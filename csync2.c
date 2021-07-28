/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  Copyright (C) 2004 - 2015 LINBIT Information Technologies GmbH
 *  http://www.linbit.com; see also AUTHORS
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
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
#include <syslog.h>
#include "db_api.h"
#include <netdb.h>

#ifdef REAL_DBDIR
#  undef DBDIR
#  define DBDIR REAL_DBDIR
#endif

char *csync_database = 0;

int db_type = DB_SQLITE3;

static char *file_config = 0;
char *cfgname = "";
char *systemdir = NULL;	/* ETCDIR */
char *lockfile = NULL;	/* ETCDIR/csync2.lock */

char myhostname[256] = "";
int bind_to_myhostname = 0;
char *csync_port = "30865";
char *active_grouplist = 0;
char *active_peerlist = 0;

extern int yyparse();
extern FILE *yyin;

int csync_error_count = 0;
int csync_debug_level = 0;
FILE *csync_debug_out = 0;
int csync_syslog = 0;

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
"Copyright (C) 2004 - 2013  LINBIT Information Technologies GmbH\n"
"Copyright (C) 2008 - 2018  LINBIT HA Solutions GmbH\n"
"        https://www.linbit.com\n"
"See also: https://github.com/LINBIT/csync2/\n"
"\n"
"Version: " CSYNC2_VERSION "\n"
"\n"
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
"	-f [-r] file..		Force files to win next conflict resolution\n"
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
"	-R	Remove files from database which do not match config entries.\n"
"\n"
"	-i	Run in inetd server mode.\n"
"	-ii	Run in stand-alone server mode.\n"
"	-iii	Run in stand-alone server mode (one connect only).\n"
"\n"
"	-l	Send some messages to syslog instead of stderr to not clobber\n"
"		the protocol in case stdout and stderr point to the same fd.\n"
"		Default for inetd mode.\n"
"\n"
"Exit codes:\n"
"	The modes -H, -L, -M and -S return 2 if the requested db is empty.\n"
"	The mode -T returns 2 if both hosts are in sync.\n"
"	Otherwise, only exit codes 0 (no errors)\n"
"	and 1 (some unspecified errrors) are expected.\n"
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
"	-N address	When running in stand-alone mode with -ii bind to a\n"
"		specific interface. You can pass either a hostname or ip\n"
"		address. If used, this value must match exactly the host\n"
"		value in each csync2.cfg file.\n"
"\n"
"	-I	Init-run. Use with care and read the documentation first!\n"
"		You usually do not need this option unless you are\n"
"		initializing groups with really large file lists.\n"
"\n"
"	-X	Also add removals to dirty db when doing a -TI run.\n"
"	-U	Don't mark all other peers as dirty when doing a -TI run.\n"
"\n"
"	-G Group1,Group2,Group3,...\n"
"		Only use these groups from config-file.\n"
"\n"
"	-P peer1,peer1,...\n"
"		Only update these peers (still mark all as dirty).\n"
"		Only show files for these peers in -o (compare) mode.\n"
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
"Database switches:\n"
"	-D database-dir or url\n"
"		default: /var/lib/csync2\n"
"		Absolute path: use sqlite database in that directory\n"
"	    URLs:\n"
"		sqlite:///some/path[/database.db3]\n"
"		sqlite3:///some/path[/database.db3]\n"
"		sqlite2:///some/path[/database.db]\n"
"		mysql://[<user>:<password>@]<hostname>/[database]\n"
"		pgsql://[<user>:<password>@]<hostname>/[database]\n"
"	If database is not given, it defaults to csync2_<qualified hostname>.\n"
"	Note that for non-sqlite backends, the database name is \"cleaned\",\n"
"	characters outside of [0-9][a-z][A-Z] will be replaced with _.\n"
"\n"
"Creating key file:\n"
"	%s -k filename\n"
"\n"
"Environment variables:\n"
"	CSYNC2_SYSTEM_DIR\n"
"		Directory containing csync2.cfg and other csync2 system files.\n"
"		Defaults to " ETCDIR ".\n"
"\n"
"Csync2 will refuse to do anything if this file is found:\n"
"$CSYNC2_SYSTEM_DIR/csync2.lock\n"
"\n",
		cmd, cmd);
	exit(1);
}

int create_keyfile(const char *filename)
{
	int fd = open(filename, O_WRONLY|O_CREAT|O_EXCL, 0600);
	int rand = open("/dev/urandom", O_RDONLY);
	char matrix[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";
	unsigned char key[64 /* plus newline */ +1];
	unsigned char key_bin[48 /* (sizeof(key)*8)/6 */];
	int i, j;
	int rc;

	assert(sizeof(matrix) == 65);
	if ( fd == -1 ) {
		fprintf(stderr, "Can't create key file: %s\n", strerror(errno));
		return 1;
	}
	if ( rand == -1 ) {
		fprintf(stderr, "Can't open /dev/urandom: %s\n", strerror(errno));
		return 1;
	}
	rc = read(rand, key_bin, sizeof(key_bin));
	if (rc != sizeof(key_bin)) {
		fprintf(stderr, "Failed to read %zu bytes from /dev/urandom: %s\n",
				sizeof(key_bin),
				rc == -1 ? strerror(errno) : "short read?");
		return -1;
	}
	close(rand);
	for (i = j = 0; i < sizeof(key)/4*4; i+=4, j+=3) {
		key[i+0] = matrix[  key_bin[j]                              & 63];
		key[i+1] = matrix[((key_bin[j]   >>  6)|(key_bin[j+1] <<2)) & 63];
		key[i+2] = matrix[((key_bin[j+1] >>  4)|(key_bin[j+2] <<4)) & 63];
		key[i+3] = matrix[ (key_bin[j+2] >>  2)                     & 63];
	}
	key[sizeof(key) -1] = '\n';
	errno = 0;
	rc = write(fd, key, sizeof(key));
	if (close(fd) || rc != sizeof(key)) {
		fprintf(stderr, "Failed to write out keyfile: %s\n",
				errno ? strerror(errno) : "short write?");
		unlink(filename);
	}
	return 0;
}

static int csync_server_bind(void)
{
	struct linger sl = { 1, 5 };
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int save_errno;
	int sfd = -1, s, off = 0, on = 1;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;	/* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	s = getaddrinfo(bind_to_myhostname ? myhostname : NULL, csync_port, &hints, &result);
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
		if (rp->ai_family == AF_INET6)
			if (setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, &off, (socklen_t) sizeof(off)) < 0)
				goto error;
		if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;	/* Success */

		close(sfd);
		sfd = -1;
	}

	if (sfd != -1) {
		char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
		if (getnameinfo(rp->ai_addr, rp->ai_addrlen,
				hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
				NI_NUMERICHOST | NI_NUMERICSERV) == 0)
			csync_debug(1, "Listening on %s:%s as %s.\n",
				hbuf, sbuf, myhostname);
		else
			/* WTF, is failure even possible here? */
			csync_debug(1, "Listening on <?>:%s as %s.\n",
				csync_port, myhostname);
	}

	freeaddrinfo(result);	/* No longer needed */

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
		unsigned addrlen = sizeof(addr);
		int conn = accept(listenfd, &addr.sa, &addrlen);
		if (conn < 0) goto error;

		fflush(stdout); fflush(stderr);

		if (single_connect)
			close(listenfd);

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
			csync_debug(1, "New connection from %s:%s.\n", hbuf, sbuf);

			dup2(conn, 0);
			dup2(conn, 1);
			close(conn);
			return 0;
		}

		close(conn);
	}

error:
	csync_debug(0, "Server error: %s\n", strerror(errno));
	return 1;
}

void csync_openlog(void)
{
	csync_syslog = 1;
	openlog("csync2", LOG_ODELAY | LOG_PID, LOG_LOCAL0);
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

	systemdir = getenv("CSYNC2_SYSTEM_DIR");
	if (!systemdir)
		systemdir = ETCDIR;

	ASPRINTF(&lockfile, "%s/csync2.lock", systemdir);
	if (!access(lockfile , F_OK)) {
		printf("Found %s\n", lockfile);
		return 1;
	}

	while ( (opt = getopt(argc, argv, "W:s:Ftp:G:P:C:D:N:HBAIXULlSTMRvhcuoimfxrd")) != -1 ) {

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
				csync_database = optarg;
				break;
			case 'N':
				snprintf(myhostname, 256, "%s", optarg);
				++bind_to_myhostname;
				break;
			case 'v':
				csync_debug_level++;
				break;
			case 'l':
				csync_openlog();
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

	/* Some inetd connect stderr to stdout.  The debug level messages on
	 * stderr would confuse the csync2 protocol. Log to syslog instead. */
	if ( mode == MODE_INETD && !csync_syslog )
		csync_openlog();

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
		static char line[4 * 4096];
		char *cmd, *para;

		/* configure conn.c for inetd mode */
		conn_set(0, 1);

		if ( !conn_gets(line, sizeof(line)) ) return 0;
		cmd = strtok(line, "\t \r\n");
		para = cmd ? strtok(0, "\t \r\n") : 0;

		if (cmd && !strcasecmp(cmd, "ssl")) {
#ifdef HAVE_LIBGNUTLS
			conn_resp(CR_OK_ACTIVATING_SSL);
			conn_activate_ssl(1);

			if ( !conn_gets(line, sizeof(line)) ) return 0;
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
	if ( !*cfgname ) {
	     ASPRINTF(&file_config, "%s/csync2.cfg", systemdir);
	} else {
		int i;

		for (i=0; cfgname[i]; i++)
			if ( !(cfgname[i] >= '0' && cfgname[i] <= '9') &&
			     !(cfgname[i] >= 'a' && cfgname[i] <= 'z') ) {
				(mode == MODE_INETD ? conn_printf : csync_fatal)
						("Config names are limited to [a-z0-9]+.\n");
				return mode != MODE_INETD;
			}

		ASPRINTF(&file_config, "%s/csync2_%s.cfg", systemdir, cfgname);
	}

	csync_debug(2, "Config-File:   %s\n", file_config);
	yyin = fopen(file_config, "r");
	if ( !yyin )
		csync_fatal("Can not open config file `%s': %s\n",
				file_config, strerror(errno));
	yyparse();
	fclose(yyin);

	if (!csync_database || !csync_database[0] || csync_database[0] == '/')
		csync_database = db_default_database(csync_database);



	// If local hostname is not set, try to guess it by getting the addrinfo of every hostname  in the
	// group and try to bind on that address. If bind is successful set that host as local hostname.
	{
		struct csync_group *g;
		struct csync_group_host *h;
		struct csync_group_host *prev = 0;
		struct addrinfo *rp, *result;
		int sfd;
		int bind_status;

		for (g=csync_group; g; g=g->next) {
			if ( !g->myname ) {
				h = g->host;
				while(h && !g->myname) {
					getaddrinfo(h->hostname, NULL, NULL, &result);
					for (rp = result; rp != NULL; rp = rp->ai_next) {
						sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
						if (sfd == -1)
							continue;
						bind_status = bind(sfd, rp->ai_addr, rp->ai_addrlen);
						close(sfd);

						if (bind_status == 0) {
							g->myname = h->hostname;
							snprintf(myhostname, 256, "%s", h->hostname);
							g->local_slave = h->slave;

							if (!prev) {
								g->host = h->next;
							} else {
								prev->next = h->next;
							}
							free(h);
							csync_debug(1, "My hostname guessed as: %s\n", g->myname);
							break;
						}
					}
					freeaddrinfo(result);
					prev = h;
					h = h->next;
				}
			}
		}
	}

	csync_debug(2, "My hostname is %s.\n", myhostname);
	csync_debug(2, "Database-File: %s\n", csync_database);


	{
		const struct csync_group *g;
		for (g=csync_group; g; g=g->next)
			if ( g->myname ) goto found_a_group;
		csync_fatal("This host (%s) is not a member of any configured group.\n", myhostname);
found_a_group:;
	}

	csync_db_open(csync_database);

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
			conn_resp(CR_OK_CMD_FINISHED);
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
						ASPRINTF(&where_rec, "or 1=1");
					else
						ASPRINTF(&where_rec, "UNION ALL SELECT filename from file where filename > '%s/' "
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
				char *pfname = strdup(prefixencode(realname));
				char *where_rec = "";

				if ( recursive ) {
					if ( !strcmp(realname, "/") )
						ASPRINTF(&where_rec, "or 1=1");
					else
						ASPRINTF(&where_rec, "or (filename > '%s/' "
							"and filename < '%s0')",
							url_encode(pfname), url_encode(pfname));
				}

				SQL("Mark file as to be forced",
					"UPDATE dirty SET forced = 1 WHERE filename = '%s' %s",
					url_encode(pfname), where_rec);

				if ( recursive )
					free(where_rec);
				free(pfname);
			}
			break;

		case MODE_LIST_HINT:
			retval = 2;
			SQL_BEGIN("DB Dump - Hint",
				"SELECT recursive, filename FROM hint ORDER BY filename")
			{
				printf("%s\t%s\n", (char*)SQL_V(0), url_decode(SQL_V(1)));
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
			char *pfname;
			if (init_run && init_run_with_removals)
				init_run |= 2;
			if (init_run && init_run_straight)
				init_run |= 4;
			switch (argc-optind)
			{
			case 3:
				realname = getrealfn(argv[optind+2]);
				csync_check_usefullness(realname, 0);

				pfname=strdup(prefixencode(realname));
				if ( mode_test_auto_diff ) {
					csync_compare_mode = 1;
					retval = csync_diff(argv[optind], argv[optind+1], pfname);
				} else
					if ( csync_insynctest(argv[optind], argv[optind+1], init_run, 0, pfname) )
						retval = 2;
				free(pfname);
				break;
			case 2:
				if ( csync_insynctest(argv[optind], argv[optind+1], init_run, mode_test_auto_diff, 0) )
					retval = 2;
				break;
			case 1:
				realname = getrealfn(argv[optind]);
				csync_check_usefullness(realname, 0);

				pfname=strdup(prefixencode(realname));
				if ( mode_test_auto_diff )
					csync_compare_mode = 1;
				if ( csync_insynctest_all(init_run, mode_test_auto_diff, pfname) )
					retval = 2;
				free(pfname);
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
				"SELECT forced, myname, peername, filename FROM dirty ORDER BY filename")
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

	csync_debug(1, "Connection closed.\n");

	if ( csync_error_count != 0 || (csync_messages_printed && csync_debug_level) )
		csync_debug(0, "Finished with %d errors.\n", csync_error_count);

	csync_printtotaltime();

	if ( retval >= 0 && csync_error_count == 0 ) return retval;
	return csync_error_count != 0;
}

