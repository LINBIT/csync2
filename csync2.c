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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *file_database = "/var/lib/csync2";
static char *file_config = "/etc/csync2.cfg";

char myhostname[256] = "";

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
	MODE_LIST_DIRTY,
	MODE_SIMPLE
};

void help(char *cmd)
{
	printf(
"\n"
"Usage: %s [-v..] [-C config-file] [-D database-dir] [-N hostname] ..\n"
"\n"
"With file parameters:\n"
"	-h [-r] [file..]	Add (recursive) hints for check to db\n"
"	-m [file..]		Mark files in database as dirty\n"
"	-f [file..]		Force this file in sync (resolve conflict)\n"
"\n"
"Simple mode:\n"
"	-x [file..]		Add files as recursive hints to the db, run\n"
"				checks for all hints in database and update\n"
"				remote hosts.\n"
"\n"
"Without file parameters:\n"
"	-c	Check all hints in db and eventually mark files as dirty\n"
"	-u	Update (transfer dirty files to peers and mark as clear)\n"
"\n"
"	-H	List all pending hints from status db\n"
"	-L	List all file-entries from status db\n"
"	-M	List all dirty files from status db\n"
"\n"
"	-i	Run in inetd server mode.\n"
"\n",
		cmd);
	exit(1);
}

int main(int argc, char ** argv)
{
	struct textlist *tl = 0, *t;
	int mode = MODE_NONE;
	int recursive = 0;
	int opt, i;

	csync_debug_out = stderr;

	while ( (opt = getopt(argc, argv, "C:D:N:HLMvhcuimrfx")) != -1 ) {
		switch (opt) {
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
				recursive = 1;
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
			case 'M':
				if ( mode != MODE_NONE ) help(argv[0]);
				mode = MODE_LIST_DIRTY;
				break;
			case 'r':
				if ( mode != MODE_HINT ) help(argv[0]);
				recursive = 1;
				break;
			default:
				help(argv[0]);
		}
	}

	if ( optind < argc && mode != MODE_HINT && mode != MODE_MARK &&
			mode != MODE_FORCE && mode != MODE_SIMPLE )
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
		case MODE_HINT:
		case MODE_SIMPLE:
			for (i=optind; i < argc; i++)
				csync_hint(getrealfn(argv[i]), recursive);
			if (mode != MODE_SIMPLE) break;

		case MODE_CHECK:
			SQL_BEGIN("Check all hints",
				"SELECT filename, recursive FROM hint")
			{
				textlist_add(&tl, url_decode(SQL_V[0]),
						atoi(SQL_V[1]));
			} SQL_END;

			for (t = tl; t != 0; t = t->next) {
				csync_check(t->value, t->intvalue);
				SQL("Remove processed hint.",
				    "DELETE FROM hint WHERE filename = '%s' "
				    "and recursive = %d",
				    url_encode(t->value), t->intvalue);
			}

			textlist_free(tl);
			if (mode != MODE_SIMPLE) break;

		case MODE_UPDATE:
			csync_update();
			break;

		case MODE_INETD:
			csync_daemon_session(stdin, stdout);
			break;

		case MODE_MARK:
			for (i=optind; i < argc; i++)
				csync_mark(getrealfn(argv[i]));
			break;

		case MODE_FORCE:
			for (i=optind; i < argc; i++)
				SQL("Mark file as to be forced",
					"UPDATE dirty SET force = 1 "
					"WHERE filename = '%s'",
					url_encode(argv[i]));
			break;

		case MODE_LIST_HINT:
			SQL_BEGIN("DB Dump - Hint",
				"SELECT recursive, filename FROM hint")
			{
				printf("%s\t%s\n", SQL_V[0],
						url_decode(SQL_V[1]));
			} SQL_END;
			break;

		case MODE_LIST_FILE:
			SQL_BEGIN("DB Dump - File",
				"SELECT checktxt, filename FROM file")
			{
				printf("%s\t%s\n",
						url_decode(SQL_V[0]),
						url_decode(SQL_V[1]));
			} SQL_END;
			break;

		case MODE_LIST_DIRTY:
			SQL_BEGIN("DB Dump - Dirty",
				"SELECT force, hostname, filename FROM dirty")
			{
				printf("%s\t%s\t%s\n",
						atoi(SQL_V[0]) ?
							"force" : "auto",
						url_decode(SQL_V[1]),
						url_decode(SQL_V[2]));
			} SQL_END;
			break;
	}

	csync_db_close();
	csync_debug(csync_error_count != 0 ? 0 : 1,
			"Finished with %d errors.\n", csync_error_count);
	return csync_error_count != 0;
}

