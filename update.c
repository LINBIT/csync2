/*
 *  csync2 - cluster syncronisation tool, 2nd generation
 *  LINBIT Information Technologies <http://www.linbit.com>
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

int read_conn_status(FILE * conn, const char *file, const char *host)
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

FILE * connect_to_host(const char * hostname)
{
	struct sockaddr_in sin;
	struct hostent *hp;
	int s;

	hp = gethostbyname(hostname);
	if ( ! hp ) {
		csync_debug(1, "Can't resolve hostname.\n");
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

void csync_update_file_del(const char *hostname,
		const char *filename, int force, FILE *conn)
{
	const char * key = csync_key(hostname, filename);

	csync_debug(1, "Deleting %s on %s ...\n", filename, hostname);

	if ( !key ) {
		csync_debug(0, "ERROR: No key for %s on %s.n",
				filename, hostname);
		csync_error_count++;
		goto got_error;
	}

	if ( force ) {
		connprintf(conn, "FLUSH %s %s\n", url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	}

	connprintf(conn, "DEL %s %s\n", url_encode(key), url_encode(filename));
	if ( read_conn_status(conn, filename, hostname) ) goto got_error;

	SQL("Remove dirty-file entry.",
		"DELETE FROM dirty WHERE filename = '%s' "
		"AND hostname = '%s'", url_encode(filename),
		url_encode(hostname));
	return;

got_error:
	csync_debug(1, "File stays in dirty state. Try again later...\n");
}

void csync_update_file_mod(const char * hostname,
		const char * filename, int force, FILE * conn)
{
	struct stat st;
	const char * key = csync_key(hostname, filename);

	csync_debug(1, "Updating %s on %s ...\n", filename, hostname);

	if ( !key ) {
		csync_debug(0, "ERROR: No key for %s on %s.\n",
				filename, hostname);
		csync_error_count++;
		goto got_error;
	}

	if ( lstat(filename, &st) != 0 ) {
		csync_debug(0, "ERROR: Cant stat %s.\n", filename);
		csync_error_count++;
		goto got_error;
	}

	if ( force ) {
		connprintf(conn, "FLUSH %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	} else {
		int i, found_diff = 0;
		char chk1[4096];
		const char *chk2;

		connprintf(conn, "SIG %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) ) goto got_error;

		if ( !fgets(chk1, 4096, conn) ) goto got_error;
		chk2 = csync_genchecktxt(&st, filename, 1);
		for (i=0; chk1[i] && chk1[i] != '\n' && chk2[i]; i++)
			if ( chk1[i] != chk2[i] ) { found_diff=1; break; }

		if ( csync_rs_check(filename, conn, S_ISREG(st.st_mode)) ) found_diff=1;
		if ( read_conn_status(conn, filename, hostname) ) goto got_error;

		if ( !found_diff ) {
			csync_debug(1, "File is already up to date on peer.\n");
			goto skip_action;
		}
	}

	if ( S_ISREG(st.st_mode) ) {
		connprintf(conn, "PATCH %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
		csync_rs_delta(filename, conn, conn);
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	} else
	if ( S_ISDIR(st.st_mode) ) {
		connprintf(conn, "MKDIR %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	} else
	if ( S_ISCHR(st.st_mode) ) {
		connprintf(conn, "MKCHR %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	} else
	if ( S_ISBLK(st.st_mode) ) {
		connprintf(conn, "MKBLK %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	} else
	if ( S_ISFIFO(st.st_mode) ) {
		connprintf(conn, "MKFIFO %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
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
			if ( read_conn_status(conn, filename, hostname) )
				goto got_error;
		}
	} else
	if ( S_ISSOCK(st.st_mode) ) {
		connprintf(conn, "MKSOCK %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	}

	connprintf(conn, "SETOWN %s %s %d %d\n",
			url_encode(key), url_encode(filename),
			st.st_uid, st.st_gid);
	if ( read_conn_status(conn, filename, hostname) )
		goto got_error;

	if ( !S_ISLNK(st.st_mode) ) {
		connprintf(conn, "SETMOD %s %s %d\n", url_encode(key),
				url_encode(filename), st.st_mode);
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;

		connprintf(conn, "SETIME %s %s %Ld\n",
				url_encode(key), url_encode(filename),
				(long long)st.st_mtime);
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	}

skip_action:
	SQL("Remove dirty-file entry.",
		"DELETE FROM dirty WHERE filename = '%s' "
		"AND hostname = '%s'", url_encode(filename),
		url_encode(hostname));
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

void csync_update_host(const char * hostname,
		const char ** patlist, int patnum, int recursive)
{
	FILE * conn;
	struct textlist *tl = 0, *t, *next_t;
	struct textlist *tl_mod = 0, **last_tn=&tl;
	struct stat st;

	SQL_BEGIN("Get files for host from dirty table",
		"SELECT filename, force FROM dirty WHERE hostname = '%s' "
		"ORDER by filename ASC", url_encode(hostname))
	{
		const char * filename = url_decode(SQL_V[0]);
		int i, use_this = patnum == 0;
		for (i=0; i<patnum && !use_this; i++)
			if ( compare_files(filename, patlist[i], recursive) ) use_this = 1;
		if (use_this)
			textlist_add(&tl, filename, atoi(SQL_V[1]));
	} SQL_END;

	/* just return if there are no files to update */
	if ( !tl ) return;

	csync_debug(1, "Updating host %s ...\n", hostname);

	if ( (conn = connect_to_host(hostname)) == 0 ) {
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
			csync_update_file_del(hostname,
					t->value, t->intvalue, conn);
			last_tn=&(t->next);
		}
	}

	for (t = tl_mod; t != 0; t = t->next) {
		csync_update_file_mod(hostname,
				t->value, t->intvalue, conn);
	}

	textlist_free(tl_mod);
	textlist_free(tl);

	connprintf(conn, "BYE\n");
	read_conn_status(conn, 0, hostname);
	fclose(conn);
}

void csync_update(const char ** patlist, int patnum, int recursive)
{
	struct textlist *tl = 0, *t;

	SQL_BEGIN("Get hosts from dirty table",
		"SELECT hostname FROM dirty GROUP BY hostname")
	{
		textlist_add(&tl, url_decode(SQL_V[0]), 0);
	} SQL_END;

	for (t = tl; t != 0; t = t->next)
		csync_update_host(t->value, patlist, patnum, recursive);

	textlist_free(tl);
}

