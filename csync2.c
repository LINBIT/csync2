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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

static char *file_database = "/var/lib/csync2";
static char *file_config = "/etc/csync2.cfg";

char myhostname[256] = "";
char *active_grouplist = 0;

extern int yyparse();
extern FILE *yyin;

int csync_error_count = 0;
int csync_debug_level = 0;
FILE *csync_debug_out = 0;

enum {
	MODE_NONE,
	MODE_HINT,
	MODE_CHECK,
	MODE_UPDATE,
	MODE_INETD,
	MODE_MARK,
	MODE_FORCE,
	MODE_LIST_HINT,
	MODE_LIST_FILE,
	MODE_LIST_SYNC,
	MODE_TEST_SYNC,
	MODE_LIST_DIRTY,
	MODE_REMOVE_OLD,
	MODE_SIMPLE
};

void help(char *cmd)
{
	printf(
"\n"
"csync2 SNAPSHOT - cluster synchronisation tool, 2nd generation\n"
"LINBIT Information Technologies GmbH <http://www.linbit.com>\n"
"Copyright (C) 2004  Clifford Wolf <clifford@clifford.at>\n"
"This program is free software under the terms of the GNU GPL.\n"
"\n"
"Usage: %s [-v..] [-C config-file] [-D database-dir] [-N hostname] ..\n"
"\n"
"With file parameters:\n"
"	-h [-r] file..		Add (recursive) hints for check to db\n"
"	-c [-r] file..		Check files and maybe add to dirty db\n"
"	-u [-d] [-r] file..	Updates files if listed in dirty db\n"
"	-f file..		Force this file in sync (resolve conflict)\n"
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
"				synchronisation pair.\n"
"\n"
"	-T myname peername	Test if this synchronisation pair is in sync.\n"
"\n"
"	The modes -H, -L, -M and -S return 2 if the requested db is empty.\n"
"	The mode -T returns 2 if both hosts are in sync.\n"
"\n"
"	-i	Run in inetd server mode.\n"
"\n"
"	-R	Remove files from database which don't match config entries.\n"
"\n"
"Modifiers:\n"
"	-r	Recursive operation over subdirectories\n"
"	-d	Dry-run on all remote update operations\n"
"\n"
"	-B	Don't block everything into big SQL transactions. This\n"
"		slows down csync2 but allows multiple csync2 processes to\n"
"		access the database at the same time. Use e.g. when slow\n"
"		lines are used or huge files are transfered.\n"
"\n"
"	-I	Init-run. Use with care and read the documentation first!\n"
"		You usually don't need this option unless you are\n"
"		initializing groups with really large file lists.\n"
"\n"
"	-G Group1,Group2,Group3,...\n"
"		Only use this groups from config-file.\n"
"\n"
"Creating key file:\n"
"	%s -k filename\n"
"\n",
		cmd, cmd);
	exit(1);
}

int create_keyfile(const char *filename)
{
	int fd = open(filename, O_WRONLY|O_CREAT|O_EXCL, 0600);
	int rand = open("/dev/random", O_RDONLY);
	char matrix[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";
	unsigned char n;
	int i;

	assert(sizeof(matrix) == 65);
	if ( fd == -1 ) {
		fprintf(stderr, "Can't create key file: %s\n", strerror(errno));
		return 1;
	}
	if ( rand == -1 ) {
		fprintf(stderr, "Can't open /dev/random: %s\n", strerror(errno));
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

int main(int argc, char ** argv)
{
	struct textlist *tl = 0, *t;
	int mode = MODE_NONE;
	int init_run = 0;
	int recursive = 0;
	int retval = -1;
	int dry_run = 0;
	int opt, i;

	csync_debug_out = stderr;

	if ( argc==3 && !strcmp(argv[1], "-k") ) {
		return create_keyfile(argv[2]);
	}

	while ( (opt = getopt(argc, argv, "G:C:D:N:HBILSTMRvhcuimfxrd")) != -1 ) {
		switch (opt) {
			case 'G':
				active_grouplist = optarg;
				break;
			case 'B':
				db_blocking_mode = 0;
				break;
			case 'I':
				init_run = 1;
				break;
			case 'C':
				file_config = optarg;
				break;
			case 'D':
				file_database = optarg;
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
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_UPDATE;
				break;
			case 'i':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_INETD;
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
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_TEST_SYNC;
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
			mode != MODE_LIST_SYNC && mode != MODE_TEST_SYNC)
		help(argv[0]);

	if ( (mode == MODE_LIST_SYNC || mode == MODE_TEST_SYNC) && optind+2 != argc )
		help(argv[0]);

	if ( mode == MODE_NONE )
		help(argv[0]);

	if ( *myhostname == 0 ) {
		gethostname(myhostname, 256);
		myhostname[255] = 0;
	}

	asprintf(&file_database, "%s/%s.db", file_database, myhostname);

	csync_debug(2, "My hostname is %s.\n", myhostname);
	csync_debug(2, "Config-File:   %s\n", file_config);
	csync_debug(2, "Database-File: %s\n", file_database);

	yyin = fopen(file_config, "r");
	if ( !yyin ) csync_fatal("Can't open config file.\n");
	yyparse();

	csync_db_open(file_database);

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
			if ( argc == optind )
			{
				SQL_BEGIN("Check all hints",
					"SELECT filename, recursive FROM hint")
				{
					textlist_add(&tl, url_decode(SQL_V[0]),
							atoi(SQL_V[1]));
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

		case MODE_INETD:
			csync_daemon_session(stdin, stdout);
			break;

		case MODE_MARK:
			for (i=optind; i < argc; i++) {
				char *realname = getrealfn(argv[i]);
				csync_check_usefullness(realname, recursive);
				csync_mark(realname, 0);
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
				printf("%s\t%s\n", SQL_V[0], url_decode(SQL_V[1]));
				retval = -1;
			} SQL_END;
			break;

		case MODE_LIST_FILE:
			retval = 2;
			SQL_BEGIN("DB Dump - File",
				"SELECT checktxt, filename FROM file ORDER BY filename")
			{
				if (csync_find_next(0, url_decode(SQL_V[1]))) {
					printf("%s\t%s\n", url_decode(SQL_V[0]), url_decode(SQL_V[1]));
					retval = -1;
				}
			} SQL_END;
			break;

		case MODE_LIST_SYNC:
			retval = 2;
			SQL_BEGIN("DB Dump - File",
				"SELECT checktxt, filename FROM file ORDER BY filename")
			{
				if ( csync_match_file_host(url_decode(SQL_V[1]), argv[optind], argv[optind+1], 0) ) {
					printf("%s\t%s\n", url_decode(SQL_V[0]), url_decode(SQL_V[1]));
					retval = -1;
				}
			} SQL_END;
			break;

		case MODE_TEST_SYNC:
			if ( csync_insynctest(argv[optind], argv[optind+1], init_run) )
				retval = 2;
			break;

		case MODE_LIST_DIRTY:
			retval = 2;
			SQL_BEGIN("DB Dump - Dirty",
				"SELECT force, myname, peername, filename FROM dirty ORDER BY filename")
			{
				if (csync_find_next(0, url_decode(SQL_V[3]))) {
					printf("%s\t%s\t%s\t%s\n", atoi(SQL_V[0]) ?  "force" : "chary",
						url_decode(SQL_V[1]), url_decode(SQL_V[2]), url_decode(SQL_V[3]));
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
	csync_debug(csync_error_count != 0 ? 0 : 1,
			"Finished with %d errors.\n", csync_error_count);
	if ( retval >= 0 && csync_error_count == 0 ) return retval;
	return csync_error_count != 0;
}

