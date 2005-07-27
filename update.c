/*
 *  csync2 - cluster synchronisation tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2004, 2005  Clifford Wolf <clifford@clifford.at>
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
#include <signal.h>

static int connection_closed_error = 1;

int read_conn_status(const char *file, const char *host)
{
	char line[4096];
	if ( conn_gets(line, 4096) ) {
		if ( !strncmp(line, "OK (", 4) ) return 0;
	} else {
		connection_closed_error = 1;
		strcpy(line, "Connection closed.\n");
	}
	if ( file )
		csync_debug(0, "While syncing file %s:\n", file);
	csync_debug(0, "ERROR from peer %s: %s", host, line);
	csync_error_count++;
	return !strcmp(line, "File is also marked dirty here!") ? 1 : 2;
}

int connect_to_host(const char *peername)
{
	int use_ssl = 1;
	struct csync_nossl *t;

	connection_closed_error = 0;

	for (t = csync_nossl; t; t=t->next) {
		if ( !fnmatch(t->pattern_from, myhostname, 0) &&
		     !fnmatch(t->pattern_to, peername, 0) ) {
			use_ssl = 0;
			break;
		}
	}

	csync_debug(1, "Connecting to host %s (%s) ...\n",
			peername, use_ssl ? "SSL" : "PLAIN");

	if ( conn_open(peername) ) return -1;

	if ( use_ssl ) {
		conn_printf("SSL\n");
		if ( read_conn_status(0, peername) ) {
			csync_debug(1, "SSL command failed.\n");
			conn_close();
			return -1;
		}
		conn_activate_ssl(0);
	}

	conn_printf("CONFIG %s\n", url_encode(cfgname));
	if ( read_conn_status(0, peername) ) {
		csync_debug(1, "Config command failed.\n");
		conn_close();
		return -1;
	}

	if (active_grouplist) {
		conn_printf("GROUP %s\n", url_encode(active_grouplist));
		if ( read_conn_status(0, peername) ) {
			csync_debug(1, "Group command failed.\n");
			conn_close();
			return -1;
		}
	}

	return 0;
}

static int get_auto_method(const char *peername, const char *filename)
{
	const struct csync_group *g = 0;
	const struct csync_group_host *h;

	while ( (g=csync_find_next(g, filename)) ) {
		for (h = g->host; h; h = h->next) {
			if (!strcmp(h->hostname, peername)) {
				if (g->auto_method == CSYNC_AUTO_METHOD_LEFT && h->on_left_side)
					return CSYNC_AUTO_METHOD_LEFT_RIGHT_LOST;
				if (g->auto_method == CSYNC_AUTO_METHOD_RIGHT && !h->on_left_side)
					return CSYNC_AUTO_METHOD_LEFT_RIGHT_LOST;
				return g->auto_method;
			}
		}
	}

	return CSYNC_AUTO_METHOD_NONE;
}

void csync_update_file_del(const char *peername,
		const char *filename, int force, int dry_run)
{
	int last_conn_status = 0, auto_resolve_run = 0;
	const char * key = csync_key(peername, filename);

	if ( !key ) {
		csync_debug(2, "Skipping deletion %s on %s - not in my groups.\n", filename, peername);
		return;
	}

auto_resolve_entry_point:
	csync_debug(1, "Deleting %s on %s ...\n", filename, peername);

	if ( force ) {
		if ( dry_run ) {
			printf("!D: %-15s %s\n", peername, filename);
			return;
		}
		conn_printf("FLUSH %s %s\n", url_encode(key), url_encode(filename));
		if ( read_conn_status(filename, peername) )
			goto got_error;
	} else {
		int i, found_diff = 0;
		int rs_check_result;
		const char *chk2 = "---";
		char chk1[4096];

		conn_printf("SIG %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(filename, peername) ) goto got_error;

		if ( !conn_gets(chk1, 4096) ) goto got_error;
		for (i=0; chk1[i] && chk1[i] != '\n' && chk2[i]; i++)
			if ( chk1[i] != chk2[i] ) {
				csync_debug(2, "File is different on peer (cktxt char #%d).\n", i);
				csync_debug(2, ">>> PEER:  %s>>> LOCAL: %s\n", chk1, chk2);
				found_diff=1;
				break;
			}

		rs_check_result = csync_rs_check(filename, 0);
		if ( rs_check_result < 0 )
			goto got_error;
		if ( rs_check_result ) {
			csync_debug(2, "File is different on peer (rsync sig).\n");
			found_diff=1;
		}
		if ( read_conn_status(filename, peername) ) goto got_error;

		if ( !found_diff ) {
			csync_debug(1, "File is already up to date on peer.\n");
			if ( dry_run ) {
				printf("?S: %-15s %s\n", peername, filename);
				return;
			}
			goto skip_action;
		}
		if ( dry_run ) {
			printf("?D: %-15s %s\n", peername, filename);
			return;
		}
	}

	conn_printf("DEL %s %s\n",
			url_encode(key), url_encode(filename));
	if ( (last_conn_status=read_conn_status(filename, peername)) )
		goto maybe_auto_resolve;

	conn_printf("MARK %s %s\n",
			url_encode(key), url_encode(filename));
	if ( read_conn_status(filename, peername) )
		goto got_error;

skip_action:
	SQL("Remove dirty-file entry.",
		"DELETE FROM dirty WHERE filename = '%s' "
		"AND peername = '%s'", url_encode(filename),
		url_encode(peername));

	if (auto_resolve_run)
		csync_error_count--;

	return;

maybe_auto_resolve:
	if (!auto_resolve_run && last_conn_status == 2)
	{
		int auto_method = get_auto_method(peername, filename);

		switch (auto_method)
		{
		case CSYNC_AUTO_METHOD_FIRST:
			auto_resolve_run = 1;
			csync_debug(0, "Auto-resolving conflict: Won 'first' test.\n");
			break;

		case CSYNC_AUTO_METHOD_LEFT:
		case CSYNC_AUTO_METHOD_RIGHT:
			auto_resolve_run = 1;
			csync_debug(0, "Auto-resolving conflict: Won 'left/right' test.\n");
			break;

		case CSYNC_AUTO_METHOD_LEFT_RIGHT_LOST:
			csync_debug(0, "Do not auto-resolve conflict: Lost 'left/right' test.\n");
			break;

		case CSYNC_AUTO_METHOD_YOUNGER:
		case CSYNC_AUTO_METHOD_OLDER:
		case CSYNC_AUTO_METHOD_BIGGER:
		case CSYNC_AUTO_METHOD_SMALLER:
			csync_debug(0, "Do not auto-resolve conflict: This is a removal.\n");
			break;
		}

		if (auto_resolve_run) {
			force = 1;
			goto auto_resolve_entry_point;
		}
	}

got_error:
	if (auto_resolve_run)
got_error_in_autoresolve:
		csync_debug(0, "ERROR: Auto-resolving failed. Giving up.\n");
	csync_debug(1, "File stays in dirty state. Try again later...\n");
}

void csync_update_file_mod(const char *peername,
		const char *filename, int force, int dry_run)
{
	struct stat st;
	int last_conn_status = 0, auto_resolve_run = 0;
	const char * key = csync_key(peername, filename);

	if ( !key ) {
		csync_debug(2, "Skipping file update %s on %s - not in my groups.\n", filename, peername);
		return;
	}

auto_resolve_entry_point:
	csync_debug(1, "Updating %s on %s ...\n", filename, peername);

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
		conn_printf("FLUSH %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(filename, peername) )
			goto got_error;
	} else {
		int i, found_diff = 0;
		int rs_check_result;
		char chk1[4096];
		const char *chk2;

		conn_printf("SIG %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(filename, peername) ) goto got_error;

		if ( !conn_gets(chk1, 4096) ) goto got_error;
		chk2 = csync_genchecktxt(&st, filename, 1);
		for (i=0; chk1[i] && chk1[i] != '\n' && chk2[i]; i++)
			if ( chk1[i] != chk2[i] ) {
				csync_debug(2, "File is different on peer (cktxt char #%d).\n", i);
				csync_debug(2, ">>> PEER:  %s>>> LOCAL: %s\n", chk1, chk2);
				found_diff=1;
				break;
			}

		rs_check_result = csync_rs_check(filename, S_ISREG(st.st_mode));
		if ( rs_check_result < 0 )
			goto got_error;
		if ( rs_check_result ) {
			csync_debug(2, "File is different on peer (rsync sig).\n");
			found_diff=1;
		}
		if ( read_conn_status(filename, peername) ) goto got_error;

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
		conn_printf("PATCH %s %s\n",
				url_encode(key), url_encode(filename));
		if ( (last_conn_status = read_conn_status(filename, peername)) )
			goto maybe_auto_resolve;
		if ( csync_rs_delta(filename) ) {
			read_conn_status(filename, peername);
			goto got_error;
		}
		if ( read_conn_status(filename, peername) )
			goto got_error;
	} else
	if ( S_ISDIR(st.st_mode) ) {
		conn_printf("MKDIR %s %s\n",
				url_encode(key), url_encode(filename));
		if ( (last_conn_status = read_conn_status(filename, peername)) )
			goto maybe_auto_resolve;
	} else
	if ( S_ISCHR(st.st_mode) ) {
		conn_printf("MKCHR %s %s\n",
				url_encode(key), url_encode(filename));
		if ( (last_conn_status = read_conn_status(filename, peername)) )
			goto maybe_auto_resolve;
	} else
	if ( S_ISBLK(st.st_mode) ) {
		conn_printf("MKBLK %s %s\n",
				url_encode(key), url_encode(filename));
		if ( (last_conn_status = read_conn_status(filename, peername)) )
			goto maybe_auto_resolve;
	} else
	if ( S_ISFIFO(st.st_mode) ) {
		conn_printf("MKFIFO %s %s\n",
				url_encode(key), url_encode(filename));
		if ( (last_conn_status = read_conn_status(filename, peername)) )
			goto maybe_auto_resolve;
	} else
	if ( S_ISLNK(st.st_mode) ) {
		char target[1024];
		int rc;
		rc = readlink(filename, target, 1023);
		if ( rc >= 0 ) {
			target[rc]=0;
			conn_printf("MKLINK %s %s %s\n",
					url_encode(key), url_encode(filename),
					url_encode(target));
			if ( (last_conn_status = read_conn_status(filename, peername)) )
				goto maybe_auto_resolve;
		}
	} else
	if ( S_ISSOCK(st.st_mode) ) {
		conn_printf("MKSOCK %s %s\n",
				url_encode(key), url_encode(filename));
		if ( (last_conn_status = read_conn_status(filename, peername)) )
			goto maybe_auto_resolve;
	}

	conn_printf("SETOWN %s %s %d %d\n",
			url_encode(key), url_encode(filename),
			st.st_uid, st.st_gid);
	if ( read_conn_status(filename, peername) )
		goto got_error;

	if ( !S_ISLNK(st.st_mode) ) {
		conn_printf("SETMOD %s %s %d\n", url_encode(key),
				url_encode(filename), st.st_mode);
		if ( read_conn_status(filename, peername) )
			goto got_error;
	}

skip_action:
	if ( !S_ISLNK(st.st_mode) ) {
		conn_printf("SETIME %s %s %Ld\n",
				url_encode(key), url_encode(filename),
				(long long)st.st_mtime);
		if ( read_conn_status(filename, peername) )
			goto got_error;
	}

	conn_printf("MARK %s %s\n",
			url_encode(key), url_encode(filename));
	if ( read_conn_status(filename, peername) )
		goto got_error;

	SQL("Remove dirty-file entry.",
		"DELETE FROM dirty WHERE filename = '%s' "
		"AND peername = '%s'", url_encode(filename),
		url_encode(peername));

	if (auto_resolve_run)
		csync_error_count--;

	return;

maybe_auto_resolve:
	if (!auto_resolve_run && last_conn_status == 2)
	{
		int auto_method = get_auto_method(peername, filename);

		switch (auto_method)
		{
		case CSYNC_AUTO_METHOD_FIRST:
			auto_resolve_run = 1;
			csync_debug(0, "Auto-resolving conflict: Won 'first' test.\n");
			break;

		case CSYNC_AUTO_METHOD_LEFT:
		case CSYNC_AUTO_METHOD_RIGHT:
			auto_resolve_run = 1;
			csync_debug(0, "Auto-resolving conflict: Won 'left/right' test.\n");
			break;

		case CSYNC_AUTO_METHOD_LEFT_RIGHT_LOST:
			csync_debug(0, "Do not auto-resolve conflict: Lost 'left/right' test.\n");
			break;

		case CSYNC_AUTO_METHOD_YOUNGER:
		case CSYNC_AUTO_METHOD_OLDER:
		case CSYNC_AUTO_METHOD_BIGGER:
		case CSYNC_AUTO_METHOD_SMALLER:
			{
				char buffer[1024], *type, *cmd;
				long remotedata, localdata;
				struct stat sbuf;

				if (auto_method == CSYNC_AUTO_METHOD_YOUNGER ||
				    auto_method == CSYNC_AUTO_METHOD_OLDER) {
					type = "younger/older";
					cmd = "GETTM";
				} else {
					type = "bigger/smaller";
					cmd = "GETSZ";
				}

				conn_printf("%s %s %s\n", cmd, url_encode(key), url_encode(filename));
				if ( read_conn_status(filename, peername) ) goto got_error_in_autoresolve;

				if ( !conn_gets(buffer, 4096) ) goto got_error_in_autoresolve;
				remotedata = atol(buffer);

				if (remotedata == -1)
					goto remote_file_has_been_removed;

				if ( lstat(filename, &sbuf) ) goto got_error_in_autoresolve;

				if (auto_method == CSYNC_AUTO_METHOD_YOUNGER ||
				    auto_method == CSYNC_AUTO_METHOD_OLDER)
					localdata = sbuf.st_mtime;
				else
					localdata = sbuf.st_size;

				if ((localdata > remotedata) ==
						(auto_method == CSYNC_AUTO_METHOD_YOUNGER ||
						 auto_method == CSYNC_AUTO_METHOD_BIGGER)) {
remote_file_has_been_removed:
					auto_resolve_run = 1;
					csync_debug(0, "Auto-resolving conflict: Won '%s' test.\n", type);
				} else
					csync_debug(0, "Do not auto-resolve conflict: Lost '%s' test.\n", type);
				break;
			}
		}

		if (auto_resolve_run) {
			force = 1;
			goto auto_resolve_entry_point;
		}
	}

got_error:
	if (auto_resolve_run)
got_error_in_autoresolve:
		csync_debug(0, "ERROR: Auto-resolving failed. Giving up.\n");
	csync_debug(1, "File stays in dirty state. Try again later...\n");
}

int compare_files(const char *filename, const char *pattern, int recursive)
{
	int i;
	if (!strcmp(pattern, "/")) return 1;
	for (i=0; filename[i] && pattern[i]; i++)
		if (filename[i] != pattern[i]) return 0;
	if ( filename[i] == '/' && !pattern[i] && recursive) return 1;
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

	if ( connect_to_host(peername) ) {
		csync_error_count++;
		csync_debug(0, "ERROR: Connection to remote host failed.\n");
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
		if ( !lstat(t->value, &st) != 0 && !csync_check_pure(t->value)) {
			*last_tn = next_t;
			t->next = tl_mod;
			tl_mod = t;
		} else {
			if ( !current_name || strcmp(current_name, t->value2) ) {
				conn_printf("HELLO %s\n", url_encode(t->value2));
				if ( read_conn_status(t->value, peername) )
					goto ident_failed_1;
				current_name = t->value2;
			}
			if (!connection_closed_error)
				csync_update_file_del(peername,
						t->value, t->intvalue, dry_run);
ident_failed_1:
			last_tn=&(t->next);
		}
	}

	for (t = tl_mod; t != 0; t = t->next) {
		if ( !current_name || strcmp(current_name, t->value2) ) {
			conn_printf("HELLO %s\n", url_encode(t->value2));
			if ( read_conn_status(t->value, peername) )
				goto ident_failed_2;
			current_name = t->value2;
		}
		if (!connection_closed_error)
			csync_update_file_mod(peername,
					t->value, t->intvalue, dry_run);
ident_failed_2:;
	}

	textlist_free(tl_mod);
	textlist_free(tl);

	conn_printf("BYE\n");
	read_conn_status(0, peername);
	conn_close();
}

void csync_update(const char ** patlist, int patnum, int recursive, int dry_run)
{
	struct textlist *tl = 0, *t;

	SQL_BEGIN("Get hosts from dirty table",
		"SELECT peername FROM dirty GROUP BY peername ORDER BY random()")
	{
		textlist_add(&tl, url_decode(SQL_V[0]), 0);
	} SQL_END;

	for (t = tl; t != 0; t = t->next) {
		if (active_peerlist) {
			int i=0, pnamelen = strlen(t->value);

			while (active_peerlist[i]) {
				if ( !strncmp(active_peerlist+i, t->value, pnamelen) &&
				     (active_peerlist[i+pnamelen] == ',' || !active_peerlist[i+pnamelen]) )
					goto found_asactive;
				while (active_peerlist[i])
					if (active_peerlist[i++]==',') break;
			}
			continue;
		}
found_asactive:
		csync_update_host(t->value, patlist, patnum, recursive, dry_run);
	}

	textlist_free(tl);
}

int csync_diff(const char *myname, const char *peername, const char *filename)
{
	FILE *p;
	void *old_sigpipe_handler;
	const struct csync_group *g = 0;
	const struct csync_group_host *h;
	char buffer[512];
	size_t rc;

	while ( (g=csync_find_next(g, filename)) ) {
		if ( !g->myname || strcmp(g->myname, myname) ) continue;
		for (h = g->host; h; h = h->next)
			if (!strcmp(h->hostname, peername)) goto found_host_check;
	}
	csync_debug(0, "Host pair + file not found in configuration.\n");
	csync_error_count++;
	return 0;
found_host_check:

	if ( connect_to_host(peername) ) {
		csync_error_count++;
		csync_debug(0, "ERROR: Connection to remote host failed.\n");
		return 0;
	}

	conn_printf("HELLO %s\n", myname);
	if ( read_conn_status(0, peername) ) goto finish;

	conn_printf("TYPE %s %s\n", g->key, filename);
	if ( read_conn_status(0, peername) ) goto finish;

	printf("--- %s:%s\n+++ %s:%s\n", peername, filename, myname, filename);
	fflush(stdout);

	snprintf(buffer, 512, "diff -u - '%s' | tail +3", filename);
	old_sigpipe_handler = signal(SIGPIPE, SIG_IGN);
	p = popen(buffer, "w");

	while ( (rc=conn_read(buffer, 512)) > 0 )
		fwrite(buffer, rc, 1, p);

	fclose(p);
	signal(SIGPIPE, old_sigpipe_handler);

finish:
	conn_close();
	return 0;
}

int csync_insynctest_readline(char **file, char **checktxt)
{
	char inbuf[2048], *tmp;

	if (*file) free(*file);
	if (*checktxt) free(*checktxt);
	*file = *checktxt = 0;

	if ( !conn_gets(inbuf, 2048) ) return 1;
	if ( inbuf[0] != 'v' ) {
		if ( !strncmp(inbuf, "OK (", 4) ) {
			csync_debug(2, "End of query results: %s", inbuf);
			return 1;
		}
		csync_error_count++;
		csync_debug(0, "ERROR from peer: %s", inbuf);
		return 1;
	}

	tmp = strtok(inbuf, "\t");
	if (tmp) *checktxt=strdup(url_decode(tmp));
	else {
		csync_error_count++;
		csync_debug(0, "Format error in reply: \\t not found!\n");
		return 1;
	}

	tmp = strtok(0, "\n");
	if (tmp) *file=strdup(url_decode(tmp));
	else {
		csync_error_count++;
		csync_debug(0, "Format error in reply: \\n not found!\n");
		return 1;
	}

	csync_debug(2, "Fetched tuple from peer: %s [%s]\n", *file, *checktxt);

	return 0;
}

int csync_insynctest(const char *myname, const char *peername, int init_run, int auto_diff, const char *filename)
{
	struct textlist *diff_list = 0, *diff_ent;
	const struct csync_group *g;
	const struct csync_group_host *h;
	char *r_file=0, *r_checktxt=0;
	int remote_reuse = 0, remote_eof = 0;
	int rel, ret = 1;

	for (g = csync_group; g; g = g->next) {
		if ( !g->myname || strcmp(g->myname, myname) ) continue;
		for (h = g->host; h; h = h->next)
			if (!strcmp(h->hostname, peername)) goto found_host_check;
	}
	csync_debug(0, "Host pair not found in configuration.\n");
	csync_error_count++;
	return 0;
found_host_check:

	if ( connect_to_host(peername) ) {
		csync_error_count++;
		csync_debug(0, "ERROR: Connection to remote host failed.\n");
		return 0;
	}

	conn_printf("HELLO %s\n", myname);
	read_conn_status(0, peername);

	conn_printf("LIST %s %s", peername, filename ? url_encode(filename) : "-");
	for (g = csync_group; g; g = g->next) {
		if ( !g->myname || strcmp(g->myname, myname) ) continue;
		for (h = g->host; h; h = h->next)
			if (!strcmp(h->hostname, peername)) goto found_host;
		continue;
found_host:
		conn_printf(" %s", g->key);
	}
	conn_printf("\n");

	SQL_BEGIN("DB Dump - File",
		"SELECT checktxt, filename FROM file %s%s%s ORDER BY filename",
			filename ? "WHERE filename = '" : "",
			filename ? url_encode(filename) : "",
			filename ? "'" : "")
	{
		char *l_file = strdup(url_decode(SQL_V[1])), *l_checktxt = strdup(url_decode(SQL_V[0]));
		if ( csync_match_file_host(l_file, myname, peername, 0) ) {
			if ( remote_eof ) {
got_remote_eof:
				if (auto_diff)
					textlist_add(&diff_list, strdup(l_file), 0);
				else
					printf("L\t%s\t%s\t%s\n", myname, peername, l_file); ret=0;
				if (init_run) csync_mark(l_file, 0);
			} else {
				if ( !remote_reuse )
					if ( csync_insynctest_readline(&r_file, &r_checktxt) )
						{ remote_eof = 1; goto got_remote_eof; }
				rel = strcmp(l_file, r_file);

				while ( rel > 0 ) {
					if (auto_diff)
						textlist_add(&diff_list, strdup(r_file), 0);
					else
						printf("R\t%s\t%s\t%s\n", myname, peername, r_file); ret=0;
					if ( csync_insynctest_readline(&r_file, &r_checktxt) )
						{ remote_eof = 1; goto got_remote_eof; }
					rel = strcmp(l_file, r_file);
				}

				if ( rel < 0 ) {
					if (auto_diff)
						textlist_add(&diff_list, strdup(l_file), 0);
					else
						printf("L\t%s\t%s\t%s\n", myname, peername, l_file); ret=0;
					if (init_run) csync_mark(l_file, 0);
					remote_reuse = 1;
				} else {
					remote_reuse = 0;
					if ( !rel ) {
						if ( strcmp(l_checktxt, r_checktxt) ) {
							if (auto_diff)
								textlist_add(&diff_list, strdup(l_file), 0);
							else
								printf("X\t%s\t%s\t%s\n", myname, peername, l_file); ret=0;
							if (init_run) csync_mark(l_file, 0);
						}
					}
				}
			}
		}
		free(l_checktxt);
		free(l_file);
	} SQL_END;

	if ( !remote_eof )
		while ( !csync_insynctest_readline(&r_file, &r_checktxt) ) {
			if (auto_diff)
				textlist_add(&diff_list, strdup(r_file), 0);
			else
				printf("R\t%s\t%s\t%s\n", myname, peername, r_file); ret=0;
		}

	if (r_file) free(r_file);
	if (r_checktxt) free(r_checktxt);

	conn_printf("BYE\n");
	read_conn_status(0, peername);
	conn_close();

	for (diff_ent=diff_list; diff_ent; diff_ent=diff_ent->next)
		csync_diff(myname, peername, diff_ent->value);
	textlist_free(diff_list);

	return ret;
}

int csync_insynctest_all(int init_run, int auto_diff, const char *filename)
{
	struct textlist *myname_list = 0, *myname;
	struct csync_group *g;
	int ret = 1;

	for (g = csync_group; g; g = g->next) {
		if ( !g->myname ) continue;
		for (myname=myname_list; myname; myname=myname->next)
			if ( !strcmp(g->myname, myname->value) ) goto skip_this_myname;
		textlist_add(&myname_list, g->myname, 0);
skip_this_myname: ;
	}

	for (myname=myname_list; myname; myname=myname->next)
	{
		struct textlist *peername_list = 0, *peername;
		struct csync_group_host *h;

		for (g = csync_group; g; g = g->next) {
			if ( !g->myname || strcmp(myname->value, g->myname) ) continue;

			for (h=g->host; h; h=h->next) {
				for (peername=peername_list; peername; peername=peername->next)
					if ( !strcmp(h->hostname, peername->value) ) goto skip_this_peername;
				textlist_add(&peername_list, h->hostname, 0);
skip_this_peername:		;
			}
		}

		for (peername=peername_list; peername; peername=peername->next) {
			csync_debug(1, "Running in-sync check for %s <-> %s.\n", myname->value, peername->value);
			if ( !csync_insynctest(myname->value, peername->value, init_run, auto_diff, filename) ) ret=0;
		}

		textlist_free(peername_list);
	}

	textlist_free(myname_list);

	return ret;
}

void csync_remove_old()
{
	struct textlist *tl = 0, *t;

	SQL_BEGIN("Query dirty DB",
	          "SELECT filename, myname, peername FROM dirty")
	{
		const struct csync_group *g = 0;
		const struct csync_group_host *h;

		const char *filename = url_decode(SQL_V[0]); 

		while ((g=csync_find_next(g, filename)) != 0) {
			if (!strcmp(g->myname, SQL_V[1]))
				for (h = g->host; h; h = h->next) {
					if (!strcmp(h->hostname, SQL_V[2]))
						goto this_dirty_record_is_ok;
				}
		}

		textlist_add2(&tl, SQL_V[0], SQL_V[2], 0);

this_dirty_record_is_ok:
		;
	} SQL_END;
	for (t = tl; t != 0; t = t->next) {
		csync_debug(1, "Removing %s (%s) from dirty db.\n", t->value, t->value2);
		SQL("Remove old file from dirty db",
		    "DELETE FROM dirty WHERE filename = '%s' AND peername = '%s'", t->value, t->value2);
	}
	textlist_free(tl);

	tl = 0;
	SQL_BEGIN("Query file DB",
	          "SELECT filename FROM file")
	{
		if (!csync_find_next(0, url_decode(SQL_V[0])))
			textlist_add(&tl, SQL_V[0], 0);
	} SQL_END;
	for (t = tl; t != 0; t = t->next) {
		csync_debug(1, "Removing %s from file db.\n", t->value);
		SQL("Remove old file from file db",
		    "DELETE FROM file WHERE filename = '%s'", t->value);
	}
	textlist_free(tl);
}

