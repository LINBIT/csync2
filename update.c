/*
 *  csync2 - cluster synchronisation tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2004  Clifford Wolf <clifford@clifford.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "csync2.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <fnmatch.h>
#include <stdarg.h>

int connprintf(FILE *stream, const char *format, ...)
{
	va_list ap;
	int rc;

	va_start(ap, format);
	rc = vfprintf(stream, format, ap);
	va_end(ap);

	if ( csync_debug_level < 3 ) return rc;

	va_start(ap, format);
	fprintf(csync_debug_out, "Local> ");
	vfprintf(csync_debug_out, format, ap);
	va_end(ap);

	return rc;
}

int read_conn_status(FILE *conn, const char *file, const char *host)
{
	char line[4096];
	if ( fgets(line, 4096, conn) ) {
		csync_debug(3, "Peer> %s", line);
		if ( !strncmp(line, "OK (", 4) ) return 0;
	} else 
		strcpy(line, "Connection closed.\n");
	if ( file )
		csync_debug(0, "While syncing file %s:\n", file);
	csync_debug(0, "ERROR from peer %s: %s", host, line);
	csync_error_count++;
	return 1;
}

FILE *connect_to_host(const char *peername)
{
	struct sockaddr_in sin;
	struct hostent *hp;
	int s;

	hp = gethostbyname(peername);
	if ( ! hp ) {
		csync_debug(1, "Can't resolve peername.\n");
		return 0;
	}

	s = socket(hp->h_addrtype, SOCK_STREAM, 0);
	if (s < 0) {
		csync_debug(1, "Can't create socket.\n");
		return 0;
	}

	sin.sin_family = hp->h_addrtype;
	bcopy(hp->h_addr, &sin.sin_addr, hp->h_length);
	sin.sin_port = htons(CSYNC_PORT);

	if (connect(s, (struct sockaddr *)&sin, sizeof (sin)) < 0) {
		csync_debug(1, "Can't connect to remote host.\n");
		return 0;
	}

	return fdopen(s, "r+");
}

void csync_update_file_del(const char *peername,
		const char *filename, int force, FILE *conn, int dry_run)
{
	const char * key = csync_key(peername, filename);

	csync_debug(1, "Deleting %s on %s ...\n", filename, peername);

	if ( !key ) {
		csync_debug(0, "ERROR: No key for %s on %s.n",
				filename, peername);
		csync_error_count++;
		goto got_error;
	}

	if ( dry_run ) {
		printf("%cD: %-15s %s\n", force ? '!' : '?', peername, filename);
		return;
	}

	if ( force ) {
		connprintf(conn, "FLUSH %s %s\n", url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, peername) )
			goto got_error;
	}

	connprintf(conn, "DEL %s %s\n",
			url_encode(key), url_encode(filename));
	if ( read_conn_status(conn, filename, peername) )
		goto got_error;

	connprintf(conn, "MARK %s %s\n",
			url_encode(key), url_encode(filename));
	if ( read_conn_status(conn, filename, peername) )
		goto got_error;

	SQL("Remove dirty-file entry.",
		"DELETE FROM dirty WHERE filename = '%s' "
		"AND peername = '%s'", url_encode(filename),
		url_encode(peername));
	return;

got_error:
	csync_debug(1, "File stays in dirty state. Try again later...\n");
}

void csync_update_file_mod(const char *peername,
		const char *filename, int force, FILE *conn, int dry_run)
{
	struct stat st;
	const char * key = csync_key(peername, filename);

	csync_debug(1, "Updating %s on %s ...\n", filename, peername);

	if ( !key ) {
		csync_debug(0, "ERROR: No key for %s on %s.\n",
				filename, peername);
		csync_error_count++;
		goto got_error;
	}

	if ( lstat(filename, &st) != 0 ) {
		csync_debug(0, "ERROR: Cant stat %s.\n", filename);
		csync_error_count++;
		goto got_error;
	}

	if ( force ) {
		if ( dry_run ) {
			printf("!M: %-15s %s\n", peername, filename);
			return;
		}
		connprintf(conn, "FLUSH %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, peername) )
			goto got_error;
	} else {
		int i, found_diff = 0;
		char chk1[4096];
		const char *chk2;

		connprintf(conn, "SIG %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, peername) ) goto got_error;

		if ( !fgets(chk1, 4096, conn) ) goto got_error;
		chk2 = csync_genchecktxt(&st, filename, 1);
		for (i=0; chk1[i] && chk1[i] != '\n' && chk2[i]; i++)
			if ( chk1[i] != chk2[i] ) {
				csync_debug(2, "File is different on peer (cktxt char #%d).\n", i);
				csync_debug(2, ">>> PEER:  %s>>> LOCAL: %s\n", chk1, chk2);
				found_diff=1;
				break;
			}

		if ( csync_rs_check(filename, conn, S_ISREG(st.st_mode)) ) {
			csync_debug(2, "File is different on peer (rsync sig).\n");
			found_diff=1;
		}
		if ( read_conn_status(conn, filename, peername) ) goto got_error;

		if ( !found_diff ) {
			csync_debug(1, "File is already up to date on peer.\n");
			if ( dry_run ) {
				printf("?S: %-15s %s\n", peername, filename);
				return;
			}
			goto skip_action;
		}
		if ( dry_run ) {
			printf("?M: %-15s %s\n", peername, filename);
			return;
		}
	}

	if ( S_ISREG(st.st_mode) ) {
		connprintf(conn, "PATCH %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, peername) )
			goto got_error;
		csync_rs_delta(filename, conn, conn);
		if ( read_conn_status(conn, filename, peername) )
			goto got_error;
	} else
	if ( S_ISDIR(st.st_mode) ) {
		connprintf(conn, "MKDIR %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, peername) )
			goto got_error;
	} else
	if ( S_ISCHR(st.st_mode) ) {
		connprintf(conn, "MKCHR %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, peername) )
			goto got_error;
	} else
	if ( S_ISBLK(st.st_mode) ) {
		connprintf(conn, "MKBLK %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, peername) )
			goto got_error;
	} else
	if ( S_ISFIFO(st.st_mode) ) {
		connprintf(conn, "MKFIFO %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, peername) )
			goto got_error;
	} else
	if ( S_ISLNK(st.st_mode) ) {
		char target[1024];
		int rc;
		rc = readlink(filename, target, 1023);
		if ( rc >= 0 ) {
			target[rc]=0;
			connprintf(conn, "MKLINK %s %s %s\n",
					url_encode(key), url_encode(filename),
					url_encode(target));
			if ( read_conn_status(conn, filename, peername) )
				goto got_error;
		}
	} else
	if ( S_ISSOCK(st.st_mode) ) {
		connprintf(conn, "MKSOCK %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, peername) )
			goto got_error;
	}

	connprintf(conn, "SETOWN %s %s %d %d\n",
			url_encode(key), url_encode(filename),
			st.st_uid, st.st_gid);
	if ( read_conn_status(conn, filename, peername) )
		goto got_error;

	if ( !S_ISLNK(st.st_mode) ) {
		connprintf(conn, "SETMOD %s %s %d\n", url_encode(key),
				url_encode(filename), st.st_mode);
		if ( read_conn_status(conn, filename, peername) )
			goto got_error;
	}

skip_action:
	if ( !S_ISLNK(st.st_mode) ) {
		connprintf(conn, "SETIME %s %s %Ld\n",
				url_encode(key), url_encode(filename),
				(long long)st.st_mtime);
		if ( read_conn_status(conn, filename, peername) )
			goto got_error;
	}

	connprintf(conn, "MARK %s %s\n",
			url_encode(key), url_encode(filename));
	if ( read_conn_status(conn, filename, peername) )
		goto got_error;

	SQL("Remove dirty-file entry.",
		"DELETE FROM dirty WHERE filename = '%s' "
		"AND peername = '%s'", url_encode(filename),
		url_encode(peername));
	return;

got_error:
	csync_debug(1, "File stays in dirty state. Try again later...\n");
}

int compare_files(const char *filename, const char *pattern, int recursive)
{
	int i;
	for (i=0; filename[i] && pattern[i]; i++)
		if (filename[i] != pattern[i]) return 0;
	if ( filename[i] == '\n' && !pattern[i] && recursive) return 1;
	if ( !filename[i] && !pattern[i]) return 1;
	return 0;
}

void csync_update_host(const char *peername,
		const char **patlist, int patnum, int recursive, int dry_run)
{
	struct textlist *tl = 0, *t, *next_t;
	struct textlist *tl_mod = 0, **last_tn=&tl;
	char *current_name = 0;
	struct stat st;
	FILE *conn;

	SQL_BEGIN("Get files for host from dirty table",
		"SELECT filename, myname, force FROM dirty WHERE peername = '%s' "
		"ORDER by filename ASC", url_encode(peername))
	{
		const char * filename = url_decode(SQL_V[0]);
		int i, use_this = patnum == 0;
		for (i=0; i<patnum && !use_this; i++)
			if ( compare_files(filename, patlist[i], recursive) ) use_this = 1;
		if (use_this)
			textlist_add2(&tl, filename, url_decode(SQL_V[1]), atoi(SQL_V[2]));
	} SQL_END;

	/* just return if there are no files to update */
	if ( !tl ) return;

	csync_debug(1, "Updating host %s ...\n", peername);

	if ( (conn = connect_to_host(peername)) == 0 ) {
		csync_error_count++;
		csync_debug(1, "ERROR: Connection to remote host failed.\n");
		csync_debug(1, "Host stays in dirty state. "
				"Try again later...\n");
		return;
	}

	/*
	 * The SQL statement above creates a linked list. Due to the
	 * way the linked list is created, it has the reversed order
	 * of the sql output. This order is good for removing stuff
	 * (deep entries first) but we need to use the original order
	 * for adding things.
	 *
	 * So I added a 2nd linked list for adding and modifying
	 * files: *tl_mod. Whever a file should be added/modified
	 * it's removed in the *tl linked list and moved to that
	 * other linked list.
	 *
	 */
	for (t = tl; t != 0; t = next_t) {
		next_t = t->next;
		if ( !lstat(t->value, &st) != 0 ) {
			*last_tn = next_t;
			t->next = tl_mod;
			tl_mod = t;
		} else {
			if ( !current_name || strcmp(current_name, t->value2) ) {
				connprintf(conn, "HELLO %s\n", url_encode(t->value2));
				if ( read_conn_status(conn, t->value, peername) )
					goto ident_failed_1;
				current_name = t->value2;
			}
			csync_update_file_del(peername,
					t->value, t->intvalue, conn, dry_run);
ident_failed_1:
			last_tn=&(t->next);
		}
	}

	for (t = tl_mod; t != 0; t = t->next) {
		if ( !current_name || strcmp(current_name, t->value2) ) {
			connprintf(conn, "HELLO %s\n", url_encode(t->value2));
			if ( read_conn_status(conn, t->value, peername) )
				goto ident_failed_2;
			current_name = t->value2;
		}
		csync_update_file_mod(peername,
				t->value, t->intvalue, conn, dry_run);
ident_failed_2:
	}

	textlist_free(tl_mod);
	textlist_free(tl);

	connprintf(conn, "BYE\n");
	read_conn_status(conn, 0, peername);
	fclose(conn);
}

void csync_update(const char ** patlist, int patnum, int recursive, int dry_run)
{
	struct textlist *tl = 0, *t;

	SQL_BEGIN("Get hosts from dirty table",
		"SELECT peername FROM dirty GROUP BY peername")
	{
		textlist_add(&tl, url_decode(SQL_V[0]), 0);
	} SQL_END;

	for (t = tl; t != 0; t = t->next)
		csync_update_host(t->value, patlist, patnum, recursive, dry_run);

	textlist_free(tl);
}

int csync_insynctest_readline(FILE *conn, char **file, char **checktxt)
{
	char inbuf[2048], *tmp;

	if (*file) free(*file);
	if (*checktxt) free(*checktxt);
	*file = *checktxt = 0;

	if ( !fgets(inbuf, 2048, conn) ) return 1;
	if ( inbuf[0] != 'v' ) {
		if ( !strncmp(inbuf, "OK (", 4) ) {
			csync_debug(2, "End of query results: %s", inbuf);
			return 1;
		}
		csync_error_count++;
		csync_debug(1, "ERROR from peer: %s", inbuf);
		return 1;
	}

	tmp = strtok(inbuf, "\t");
	if (tmp) *checktxt=strdup(url_decode(tmp));
	else {
		csync_error_count++;
		csync_debug(1, "Format error in reply: \\t not found!\n");
		return 1;
	}

	tmp = strtok(0, "\n");
	if (tmp) *file=strdup(url_decode(tmp));
	else {
		csync_error_count++;
		csync_debug(1, "Format error in reply: \\n not found!\n");
		return 1;
	}

	csync_debug(2, "Fetched tuple from peer: %s [%s]\n", *file, *checktxt);

	return 0;
}

int csync_insynctest(const char *myname, const char *peername)
{
	FILE *conn;
	const struct csync_group *g;
	const struct csync_group_host *h;
	char *r_file=0, *r_checktxt=0;
	int remote_reuse = 0, remote_eof = 0;
	int rel, ret = 1;

	if ( (conn = connect_to_host(peername)) == 0 ) {
		csync_error_count++;
		csync_debug(1, "ERROR: Connection to remote host failed.\n");
		csync_debug(1, "Host stays in dirty state. "
				"Try again later...\n");
		return 0;
	}

	connprintf(conn, "HELLO %s\n", myname);
	read_conn_status(conn, 0, peername);

	connprintf(conn, "LIST %s", peername);
	for (g = csync_group; g; g = g->next) {
		if ( !g->myname || strcmp(g->myname, myname) ) continue;
		for (h = g->host; h; h = h->next)
			if (!strcmp(h->hostname, peername)) goto found_host;
		continue;
found_host:
		connprintf(conn, " %s", g->key);
	}
	connprintf(conn, "\n");

	SQL_BEGIN("DB Dump - File",
		"SELECT checktxt, filename FROM file ORDER BY filename")
	{
		const char *l_file = url_decode(SQL_V[1]), *l_checktxt = url_decode(SQL_V[0]);
		if ( csync_match_file_host(l_file, myname, peername, 0) ) {
			if ( remote_eof ) {
got_remote_eof:
				printf("L %s\n", l_file); ret=0;
			} else {
				if ( !remote_reuse )
					if ( csync_insynctest_readline(conn, &r_file, &r_checktxt) )
						{ remote_eof = 1; goto got_remote_eof; }
				rel = strcmp(l_file, r_file);

				while ( rel > 0 ) {
					printf("R %s\n", r_file); ret=0;
					if ( csync_insynctest_readline(conn, &r_file, &r_checktxt) )
						{ remote_eof = 1; goto got_remote_eof; }
					rel = strcmp(l_file, r_file);
				}

				if ( rel < 0 ) {
					printf("L %s\n", l_file); ret=0;
					remote_reuse = 1;
				} else {
					remote_reuse = 0;
					if ( !rel ) {
						if ( strcmp(l_checktxt, r_checktxt) )
							{ printf("X %s\n", l_file); ret=0; }
					}
				}
			}
		}
	} SQL_END;

	if ( !remote_eof )
		while ( !csync_insynctest_readline(conn, &r_file, &r_checktxt) )
			{ printf("R %s\n", r_file); ret=0; }

	if (r_file) free(r_file);
	if (r_checktxt) free(r_checktxt);

	connprintf(conn, "BYE\n");
	read_conn_status(conn, 0, peername);
	fclose(conn);

	return ret;
}

