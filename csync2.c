/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2004, 2005, 2006  Clifford Wolf <clifford@clifford.at>
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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <netdb.h>

#ifdef REAL_DBDIR
#  undef DBDIR
#  define DBDIR REAL_DBDIR
#endif

static char *file_database = 0;
static char *file_config = 0;
static char *dbdir = DBDIR;
char *cfgname = "";

char myhostname[256] = "";
char *csync_port = "30865";
char *active_grouplist = 0;
char *active_peerlist = 0;

extern int yyparse();
extern FILE *yyin;

int csync_error_count = 0;
int csync_debug_level = 0;
FILE *csync_debug_out = 0;

int csync_server_child_pid = 0;
int csync_timestamps = 0;
int csync_new_force = 0;

int csync_dump_dir_fd = -1;

enum {
	MODE_NONE,
	MODE_HINT,
	MODE_CHECK,
	MODE_CHECK_AND_UPDATE,
	MODE_UPDATE,
	MODE_INETD,
	MODE_SERVER,
	MODE_SINGLE,
	MODE_MARK,
	MODE_FORCE,
	MODE_LIST_HINT,
	MODE_LIST_FILE,
	MODE_LIST_SYNC,
	MODE_TEST_SYNC,
	MODE_LIST_DIRTY,
	MODE_REMOVE_OLD,
	MODE_COMPARE,
	MODE_SIMPLE
};

void help(char *cmd)
{
	printf(
"\n"
PACKAGE_STRING " - cluster synchronization tool, 2nd generation\n"
"LINBIT Information Technologies GmbH <http://www.linbit.com>\n"
"Copyright (C) 2004, 2005  Clifford Wolf <clifford@clifford.at>\n"
"This program is free software under the terms of the GNU GPL.\n"
"\n"
"Usage: %s [-v..] [-C config-name] \\\n"
"		[-D database-dir] [-N hostname] [-p port] ..\n"
"\n"
"With file parameters:\n"
"	-h [-r] file..		Add (recursive) hints for check to db\n"
"	-c [-r] file..		Check files and maybe add to dirty db\n"
"	-u [-d] [-r] file..	Updates files if listed in dirty db\n"
"	-o [-r] file..		Create list of files in compare-mode\n"
"	-f [-r] file..		Force this file in sync (resolve conflict)\n"
"	-m file..		Mark files in database as dirty\n"
"\n"
"Simple mode:\n"
"	-x [-d] [[-r] file..]	Run checks for all given files and update\n"
"				remote hosts.\n"
"\n"
"Without file parameters:\n"
"	-c	Check all hints in db and eventually mark files as dirty\n"
"	-u [-d]	Update (transfer dirty files to peers and mark as clear)\n"
"\n"
"	-H	List all pending hints from status db\n"
"	-L	List all file-entries from status db\n"
"	-M	List all dirty files from status db\n"
"\n"
"	-S myname peername	List file-entries from status db for this\n"
"				synchronization pair.\n"
"\n"
"	-T  			Test if everything is in sync with all peers.\n"
"\n"
"	-T filename 		Test if this file is in sync with all peers.\n"
"\n"
"	-T myname peername	Test if this synchronization pair is in sync.\n"
"\n"
"	-T myname peer file	Test only this file in this sync pair.\n"
"\n"
"	-TT	As -T, but print the unified diffs.\n"
"\n"
"	The modes -H, -L, -M and -S return 2 if the requested db is empty.\n"
"	The mode -T returns 2 if both hosts are in sync.\n"
"\n"
"	-i	Run in inetd server mode.\n"
"	-ii	Run in stand-alone server mode.\n"
"	-iii	Run in stand-alone server mode (one connect only).\n"
"\n"
"	-R	Remove files from database which do not match config entries.\n"
"\n"
"Modifiers:\n"
"	-r	Recursive operation over subdirectories\n"
"	-d	Dry-run on all remote update operations\n"
"\n"
"	-B	Do not block everything into big SQL transactions. This\n"
"		slows down csync2 but allows multiple csync2 processes to\n"
"		access the database at the same time. Use e.g. when slow\n"
"		lines are used or huge files are transferred.\n"
"\n"
"	-A	Open database in asynchronous mode. This will cause data\n"
"		corruption if the operating system crashes or the computer\n"
"		loses power.\n"
"\n"
"	-I	Init-run. Use with care and read the documentation first!\n"
"		You usually do not need this option unless you are\n"
"		initializing groups with really large file lists.\n"
"\n"
"	-X	Also add removals to dirty db when doing a -TI run.\n"
"	-U	Don't mark all other peers as dirty when doing a -TI run.\n"
"\n"
"	-G Group1,Group2,Group3,...\n"
"		Only use this groups from config-file.\n"
"\n"
"	-P peer1,peer1,...\n"
"		Only update this peers (still mark all as dirty).\n"
"		Only show files for this peers in -o (compare) mode.\n"
"\n"
"	-F	Add new entries to dirty database with force flag set.\n"
"\n"
"	-t	Print timestamps to debug output (e.g. for profiling).\n"
"\n"
"	-s filename\n"
"		Print timestamps also to this file.\n"
"\n"
"	-W fd	Write a list of directories in which relevant files can be\n"
"		found to the specified file descriptor (when doing a -c run).\n"
"		The directory names in this output are zero-terminated.\n"
"\n"
"Creating key file:\n"
"	%s -k filename\n"
"\n"
"Csync2 will refuse to do anything when a " ETCDIR "/csync2.lock file is found.\n"
"\n",
		cmd, cmd);
	exit(1);
}

int create_keyfile(const char *filename)
{
	int fd = open(filename, O_WRONLY|O_CREAT|O_EXCL, 0600);
	int rand = open("/dev/urandom", O_RDONLY);
	char matrix[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";
	unsigned char n;
	int i;

	assert(sizeof(matrix) == 65);
	if ( fd == -1 ) {
		fprintf(stderr, "Can't create key file: %s\n", strerror(errno));
		return 1;
	}
	if ( rand == -1 ) {
		fprintf(stderr, "Can't open /dev/urandom: %s\n", strerror(errno));
		return 1;
	}
	for (i=0; i<64; i++) {
		read(rand, &n, 1);
		write(fd, matrix+(n&63), 1);
	}
	write(fd, "\n", 1);
	close(rand);
	close(fd);
	return 0;
}

static int csync_server_bind(void)
{
	struct linger sl = { 1, 5 };
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int save_errno;
	int sfd, s, on = 1;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;	/* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	s = getaddrinfo(NULL, csync_port, &hints, &result);
	if (s != 0) {
		csync_debug(1, "Cannot prepare local socket, getaddrinfo: %s\n", gai_strerror(s));
		return -1;
	}

	/* getaddrinfo() returns a list of address structures.
	   Try each address until we successfully bind(2).
	   If socket(2) (or bind(2)) fails, we (close the socket
	   and) try the next address. */

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &on, (socklen_t) sizeof(on)) < 0)
			goto error;
		if (setsockopt(sfd, SOL_SOCKET, SO_LINGER, &sl, (socklen_t) sizeof(sl)) < 0)
			goto error;
		if (setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, &on, (socklen_t) sizeof(on)) < 0)
			goto error;

		if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;	/* Success */

		close(sfd);
	}

	freeaddrinfo(result);	/* No longer needed */

	if (rp == NULL)	/* No address succeeded */
		return -1;

	return sfd;

error:
	save_errno = errno;
	close(sfd);
	errno = save_errno;
	return -1;
}

static int csync_server_loop(int single_connect)
{
	union {
		struct sockaddr sa;
		struct sockaddr_in sa_in;
		struct sockaddr_in6 sa_in6;
		struct sockaddr_storage ss;
	} addr;
	int listenfd = csync_server_bind();
	if (listenfd < 0) goto error;

	if (listen(listenfd, 5) < 0) goto error;

	/* we want to "cleanly" shutdown if the connection is lost unexpectedly */
	signal(SIGPIPE, SIG_IGN);
	/* server is not interested in its childs, prevent zombies */
	signal(SIGCHLD, SIG_IGN);

	printf("Csync2 daemon running. Waiting for connections.\n");

	while (1) {
		int addrlen = sizeof(addr);
		int conn = accept(listenfd, &addr.sa, &addrlen);
		if (conn < 0) goto error;

		fflush(stdout); fflush(stderr);

		if (single_connect || !fork()) {
			char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
			/* need to restore default SIGCHLD handler in the session,
			 * as we may need to wait on them in action.c */
			signal(SIGCHLD, SIG_DFL);
			csync_server_child_pid = getpid();
			if (getnameinfo(&addr.sa, addrlen,
					hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
					NI_NUMERICHOST | NI_NUMERICSERV) != 0)
				goto error;
			fprintf(stderr, "<%d> New connection from %s:%s.\n",
				csync_server_child_pid, hbuf, sbuf);
			fflush(stderr);

			dup2(conn, 0);
			dup2(conn, 1);
			close(conn);
			return 0;
		}

		close(conn);
	}

error:
	fprintf(stderr, "Server error: %s\n", strerror(errno));
	return 1;
}

int main(int argc, char ** argv)
{
	struct textlist *tl = 0, *t;
	int mode = MODE_NONE;
	int mode_test_auto_diff = 0;
	int init_run = 0;
	int init_run_with_removals = 0;
	int init_run_straight = 0;
	int recursive = 0;
	int retval = -1;
	int dry_run = 0;
	int opt, i;

	csync_debug_out = stderr;

	if ( argc==3 && !strcmp(argv[1], "-k") ) {
		return create_keyfile(argv[2]);
	}

	if (!access(ETCDIR "/csync2.lock", F_OK)) {
		printf("Found " ETCDIR "/csync2.lock.\n");
		return 1;
	}

	while ( (opt = getopt(argc, argv, "W:s:Ftp:G:P:C:D:N:HBAIXULSTMRvhcuoimfxrd")) != -1 ) {
		switch (opt) {
			case 'W':
				csync_dump_dir_fd = atoi(optarg);
				if (write(csync_dump_dir_fd, 0, 0) < 0)
					csync_fatal("Invalid dump_dir_fd %d: %s\n",
							csync_dump_dir_fd, strerror(errno));
				break;
			case 's':
				csync_timestamp_out = fopen(optarg, "a");
				if (!csync_timestamp_out)
					csync_fatal("Can't open timestanp file `%s': %s\n",
							optarg, strerror(errno));
				break;
			case 'F':
				csync_new_force = 1;
				break;
			case 't':
				csync_timestamps = 1;
				break;
			case 'p':
				csync_port = strdup(optarg);
				break;
			case 'G':
				active_grouplist = optarg;
				break;
			case 'P':
				active_peerlist = optarg;
				break;
			case 'B':
				db_blocking_mode = 0;
				break;
			case 'A':
				db_sync_mode = 0;
				break;
			case 'I':
				init_run = 1;
				break;
			case 'X':
				init_run_with_removals = 1;
				break;
			case 'U':
				init_run_straight = 1;
				break;
			case 'C':
				cfgname = optarg;
				break;
			case 'D':
				dbdir = optarg;
				break;
			case 'N':
				snprintf(myhostname, 256, "%s", optarg);
				break;
			case 'v':
				csync_debug_level++;
				break;
			case 'h':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_HINT;
				break;
			case 'x':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_SIMPLE;
				break;
			case 'c':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_CHECK;
				break;
			case 'u':
				if ( mode == MODE_CHECK )
					mode = MODE_CHECK_AND_UPDATE;
				else {
					if ( mode != MODE_NONE ) help(argv[0]);
					mode = MODE_UPDATE;
				}
				break;
			case 'o':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_COMPARE;
				break;
			case 'i':
				if ( mode == MODE_INETD )
					mode = MODE_SERVER;
				else
				if ( mode == MODE_SERVER )
					mode = MODE_SINGLE;
				else {
					if ( mode != MODE_NONE ) help(argv[0]);
					mode = MODE_INETD;
				}
				break;
			case 'm':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_MARK;
				break;
			case 'f':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_FORCE;
				break;
			case 'H':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_LIST_HINT;
				break;
			case 'L':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_LIST_FILE;
				break;
			case 'S':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_LIST_SYNC;
				break;
			case 'T':
				if ( mode == MODE_TEST_SYNC ) {
					mode_test_auto_diff = 1;
				} else {
					if ( mode != MODE_NONE ) help(argv[0]);
					mode = MODE_TEST_SYNC;
				}
				break;
			case 'M':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_LIST_DIRTY;
				break;
			case 'R':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_REMOVE_OLD;
				break;
			case 'r':
				recursive = 1;
				break;
			case 'd':
				dry_run = 1;
				break;
			default:
				help(argv[0]);
		}
	}

	if ( optind < argc &&
			mode != MODE_HINT && mode != MODE_MARK &&
			mode != MODE_FORCE && mode != MODE_SIMPLE &&
			mode != MODE_UPDATE && mode != MODE_CHECK &&
			mode != MODE_COMPARE &&
			mode != MODE_CHECK_AND_UPDATE &&
			mode != MODE_LIST_SYNC && mode != MODE_TEST_SYNC)
		help(argv[0]);

	if ( mode == MODE_TEST_SYNC && optind != argc &&
	     optind+1 != argc && optind+2 != argc && optind+3 != argc)
		help(argv[0]);

	if ( mode == MODE_LIST_SYNC && optind+2 != argc )
		help(argv[0]);

	if ( mode == MODE_NONE )
		help(argv[0]);

	if ( *myhostname == 0 ) {
		gethostname(myhostname, 256);
		myhostname[255] = 0;
	}

	for (i=0; myhostname[i]; i++)
		myhostname[i] = tolower(myhostname[i]);

	/* Stand-alone server mode. This is a hack..
	 */
	if ( mode == MODE_SERVER || mode == MODE_SINGLE ) {
		if (csync_server_loop(mode == MODE_SINGLE)) return 1;
		mode = MODE_INETD;
	}

	// print time (if -t is set)
	csync_printtime();

	/* In inetd mode we need to read the module name from the peer
	 * before we open the config file and database
	 */
	if ( mode == MODE_INETD ) {
		char line[4096], *cmd, *para;

		/* configure conn.c for inetd mode */
		conn_set(0, 1);

		if ( !conn_gets(line, 4096) ) return 0;
		cmd = strtok(line, "\t \r\n");
		para = cmd ? strtok(0, "\t \r\n") : 0;

		if (cmd && !strcasecmp(cmd, "ssl")) {
#ifdef HAVE_LIBGNUTLS
			conn_printf("OK (activating_ssl).\n");
			conn_activate_ssl(1);

			if ( !conn_gets(line, 4096) ) return 0;
			cmd = strtok(line, "\t \r\n");
			para = cmd ? strtok(0, "\t \r\n") : 0;
#else
			conn_printf("This csync2 server is built without SSL support.\n");
			return 0;
#endif
		}

		if (!cmd || strcasecmp(cmd, "config")) {
			conn_printf("Expecting SSL (optional) and CONFIG as first commands.\n");
			return 0;
		}

		if (para)
			cfgname = strdup(url_decode(para));
	}

#if defined(HAVE_LIBSQLITE)
#define DBEXTENSION ".db"
#endif
#if defined(HAVE_LIBSQLITE3)
#define DBEXTENSION ".db3"
#endif
	if ( !*cfgname ) {
		asprintf(&file_database, "%s/%s" DBEXTENSION, dbdir, myhostname);
		asprintf(&file_config, ETCDIR "/csync2.cfg");
	} else {
		int i;

		for (i=0; cfgname[i]; i++)
			if ( !(cfgname[i] >= '0' && cfgname[i] <= '9') &&
			     !(cfgname[i] >= 'a' && cfgname[i] <= 'z') ) {
				(mode == MODE_INETD ? conn_printf : csync_fatal)
						("Config names are limited to [a-z0-9]+.\n");
				return mode != MODE_INETD;
			}

		asprintf(&file_database, "%s/%s_%s" DBEXTENSION, dbdir, myhostname, cfgname);
		asprintf(&file_config, ETCDIR "/csync2_%s.cfg", cfgname);
	}

	csync_debug(2, "My hostname is %s.\n", myhostname);
	csync_debug(2, "Database-File: %s\n", file_database);
	csync_debug(2, "Config-File:   %s\n", file_config);

	yyin = fopen(file_config, "r");
	if ( !yyin )
		csync_fatal("Can not open config file `%s': %s\n",
				file_config, strerror(errno));
	yyparse();
	fclose(yyin);

	{
		const struct csync_group *g;
		for (g=csync_group; g; g=g->next)
			if ( g->myname ) goto found_a_group;
		csync_fatal("This host (%s) is not a member of any configured group.\n", myhostname);
found_a_group:;
	}

	csync_db_open(file_database);

	for (i=optind; i < argc; i++)
		on_cygwin_lowercase(argv[i]);

	switch (mode) {
		case MODE_SIMPLE:
			if ( argc == optind )
			{
				csync_check("/", 1, init_run);
				csync_update(0, 0, 0, dry_run);
			}
			else
			{
				char *realnames[argc-optind];
				for (i=optind; i < argc; i++) {
					realnames[i-optind] = strdup(getrealfn(argv[i]));
					csync_check_usefullness(realnames[i-optind], recursive);
					csync_check(realnames[i-optind], recursive, init_run);
				}
				csync_update((const char**)realnames, argc-optind, recursive, dry_run);
				for (i=optind; i < argc; i++)
					free(realnames[i-optind]);
			}
			break;

		case MODE_HINT:
			for (i=optind; i < argc; i++) {
				char *realname = getrealfn(argv[i]);
				csync_check_usefullness(realname, recursive);
				csync_hint(realname, recursive);
			}
			break;

		case MODE_CHECK:
		case MODE_CHECK_AND_UPDATE:
			if ( argc == optind )
			{
				SQL_BEGIN("Check all hints",
					"SELECT filename, recursive FROM hint")
				{
					textlist_add(&tl, url_decode(SQL_V(0)),
							atoi(SQL_V(1)));
				} SQL_END;

				for (t = tl; t != 0; t = t->next) {
					csync_check(t->value, t->intvalue, init_run);
					SQL("Remove processed hint.",
					    "DELETE FROM hint WHERE filename = '%s' "
					    "and recursive = %d",
					    url_encode(t->value), t->intvalue);
				}

				textlist_free(tl);
			}
			else
			{
				for (i=optind; i < argc; i++) {
					char *realname = getrealfn(argv[i]);
					csync_check_usefullness(realname, recursive);
					csync_check(realname, recursive, init_run);
				}
			}
			if (mode != MODE_CHECK_AND_UPDATE)
				break;

		case MODE_UPDATE:
			if ( argc == optind )
			{
				csync_update(0, 0, 0, dry_run);
			}
			else
			{
				char *realnames[argc-optind];
				for (i=optind; i < argc; i++) {
					realnames[i-optind] = strdup(getrealfn(argv[i]));
					csync_check_usefullness(realnames[i-optind], recursive);
				}
				csync_update((const char**)realnames, argc-optind, recursive, dry_run);
				for (i=optind; i < argc; i++)
					free(realnames[i-optind]);
			}
			break;

		case MODE_COMPARE:
			csync_compare_mode = 1;
			for (i=optind; i < argc; i++) {
				char *realname = getrealfn(argv[i]);
				csync_check_usefullness(realname, recursive);
				csync_check(realname, recursive, init_run);
			}
			break;

		case MODE_INETD:
			conn_printf("OK (cmd_finished).\n");
			csync_daemon_session();
			break;

		case MODE_MARK:
			for (i=optind; i < argc; i++) {
				char *realname = getrealfn(argv[i]);
				char *pfname;
				csync_check_usefullness(realname, recursive);
				pfname=strdup(prefixencode(realname));
				csync_mark(pfname, 0, 0);

				if ( recursive ) {
					char *where_rec = "";

					if ( !strcmp(realname, "/") )
						asprintf(&where_rec, "or 1");
					else
						asprintf(&where_rec, "UNION ALL SELECT filename from file where filename > '%s/' "
							"and filename < '%s0'",
							url_encode(pfname), url_encode(pfname));

					SQL_BEGIN("Adding dirty entries recursively",
						"SELECT filename FROM file WHERE filename = '%s' %s",
						url_encode(pfname), where_rec)
					{
						char *filename = strdup(url_decode(SQL_V(0)));
						csync_mark(filename, 0, 0);
						free(filename);
					} SQL_END;
				}
				free(pfname);
			}
			break;

		case MODE_FORCE:
			for (i=optind; i < argc; i++) {
				char *realname = getrealfn(argv[i]);
				char *where_rec = "";

				if ( recursive ) {
					if ( !strcmp(realname, "/") )
						asprintf(&where_rec, "or 1");
					else
						asprintf(&where_rec, "or (filename > '%s/' "
							"and filename < '%s0')",
							url_encode(realname), url_encode(realname));
				}

				SQL("Mark file as to be forced",
					"UPDATE dirty SET force = 1 WHERE filename = '%s' %s",
					url_encode(realname), where_rec);

				if ( recursive )
					free(where_rec);
			}
			break;

		case MODE_LIST_HINT:
			retval = 2;
			SQL_BEGIN("DB Dump - Hint",
				"SELECT recursive, filename FROM hint ORDER BY filename")
			{
				printf("%s\t%s\n", SQL_V(0), url_decode(SQL_V(1)));
				retval = -1;
			} SQL_END;
			break;

		case MODE_LIST_FILE:
			retval = 2;
			SQL_BEGIN("DB Dump - File",
				"SELECT checktxt, filename FROM file ORDER BY filename")
			{
				if (csync_find_next(0, url_decode(SQL_V(1)))) {
					printf("%s\t%s\n", url_decode(SQL_V(0)), url_decode(SQL_V(1)));
					retval = -1;
				}
			} SQL_END;
			break;

		case MODE_LIST_SYNC:
			retval = 2;
			SQL_BEGIN("DB Dump - File",
				"SELECT checktxt, filename FROM file ORDER BY filename")
			{
				if ( csync_match_file_host(url_decode(SQL_V(1)), argv[optind], argv[optind+1], 0) ) {
					printf("%s\t%s\n", url_decode(SQL_V(0)), url_decode(SQL_V(1)));
					retval = -1;
				}
			} SQL_END;
			break;

		case MODE_TEST_SYNC: {
			char *realname;
			if (init_run && init_run_with_removals)
				init_run |= 2;
			if (init_run && init_run_straight)
				init_run |= 4;
			switch (argc-optind)
			{
			case 3:
				realname = getrealfn(argv[optind+2]);
				csync_check_usefullness(realname, 0);

				if ( mode_test_auto_diff ) {
					csync_compare_mode = 1;
					retval = csync_diff(argv[optind], argv[optind+1], realname);
				} else
					if ( csync_insynctest(argv[optind], argv[optind+1], init_run, 0, realname) )
						retval = 2;
				break;
			case 2:
				if ( csync_insynctest(argv[optind], argv[optind+1], init_run, mode_test_auto_diff, 0) )
					retval = 2;
				break;
			case 1:
				realname = getrealfn(argv[optind]);
				csync_check_usefullness(realname, 0);

				if ( mode_test_auto_diff )
					csync_compare_mode = 1;
				if ( csync_insynctest_all(init_run, mode_test_auto_diff, realname) )
					retval = 2;
				break;
			case 0:
				if ( csync_insynctest_all(init_run, mode_test_auto_diff, 0) )
					retval = 2;
				break;
			}
			break;
		}

		case MODE_LIST_DIRTY:
			retval = 2;
			SQL_BEGIN("DB Dump - Dirty",
				"SELECT force, myname, peername, filename FROM dirty ORDER BY filename")
			{
				if (csync_find_next(0, url_decode(SQL_V(3)))) {
					printf("%s\t%s\t%s\t%s\n", atoi(SQL_V(0)) ?  "force" : "chary",
						url_decode(SQL_V(1)), url_decode(SQL_V(2)), url_decode(SQL_V(3)));
					retval = -1;
				}
			} SQL_END;
			break;

		case MODE_REMOVE_OLD:
			if ( active_grouplist )
				csync_fatal("Never run -R with -G!\n");
			csync_remove_old();
			break;
	}

	csync_run_commands();
	csync_db_close();

	if ( csync_server_child_pid ) {
		fprintf(stderr, "<%d> Connection closed.\n",
				csync_server_child_pid);
		fflush(stderr);
	}

	if ( csync_error_count != 0 || (csync_messages_printed && csync_debug_level) )
		csync_debug(0, "Finished with %d errors.\n", csync_error_count);

	csync_printtotaltime();

	if ( retval >= 0 && csync_error_count == 0 ) return retval;
	return csync_error_count != 0;
}

