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
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <unistd.h>
#include <utime.h>
#include <errno.h>

static char * cmd_error;

int csync_unlink(const char * filename, int ign)
{
	struct stat st;
	int rc;

	if ( lstat(filename, &st) != 0 ) return 0;
	if ( ign==2 && S_ISREG(st.st_mode) ) return 0;
	rc = S_ISDIR(st.st_mode) ? rmdir(filename) : unlink(filename);

	if ( rc && !ign ) cmd_error = strerror(errno);
	return rc;
}

int csync_check_dirty(const char *filename)
{
	int rc = 0;
	csync_check(filename, 0);
	SQL_BEGIN("Check if file is dirty",
		"SELECT 1 FROM dirty WHERE filename = '%s' LIMIT 1",
		url_encode(filename))
	{
		rc = 1;
		cmd_error = "File is also marked dirty here!";
	} SQL_END;
	return rc;
}

void csync_file_update(const char *filename)
{
	struct stat st;
	if ( lstat(filename, &st) != 0 ) {
		SQL("Removing file from file db",
			"delete from file where filename ='%s'",
			url_encode(filename));
	} else {
		const char *checktxt = csync_genchecktxt(&st, filename, 0);
		SQL("Insert record to file db",
			"insert into file (filename, checktxt) values "
			"('%s', '%s')", url_encode(filename),
			url_encode(checktxt));
	}
}

void csync_file_flush(const char *filename)
{
	SQL("Removing file from dirty db",
		"delete from dirty where filename ='%s'",
		url_encode(filename));
}

struct csync_command {
	char *text;
	int check_perm;
	int check_dirty;
	int unlink;
	int update;
	int action;
};

enum {
	A_SIG, A_FLUSH, A_DEL, A_PATCH,
	A_MKDIR, A_MKCHR, A_MKBLK, A_MKFIFO, A_MKLINK, A_MKSOCK,
	A_SETOWN, A_SETMOD, A_SETIME,
	A_DEBUG, A_BYE
};


struct csync_command cmdtab[] = {
	{ "sig",	1, 0, 0, 0, A_SIG	},
	{ "flush",	1, 0, 0, 0, A_FLUSH	},
	{ "del",	1, 1, 0, 1, A_DEL	},
	{ "patch",	1, 1, 2, 1, A_PATCH	},
	{ "mkdir",	1, 1, 1, 1, A_MKDIR	},
	{ "mkchr",	1, 1, 1, 1, A_MKCHR	},
	{ "mkblk",	1, 1, 1, 1, A_MKBLK	},
	{ "mkfifo",	1, 1, 1, 1, A_MKFIFO	},
	{ "mklink",	1, 1, 1, 1, A_MKLINK	},
	{ "mksock",	1, 1, 1, 1, A_MKSOCK	},
	{ "setown",	1, 1, 0, 1, A_SETOWN	},
	{ "setmod",	1, 1, 0, 1, A_SETMOD	},
	{ "setime",	1, 1, 0, 1, A_SETIME	},
	{ "debug",	0, 0, 0, 0, A_DEBUG	},
	{ "bye",	0, 0, 0, 0, A_BYE	},
	{ 0,		0, 0, 0, 0, 0		}
};

void csync_daemon_session(FILE * in, FILE * out)
{
	char line[4096];
	char * tag[32];
	int i;

	while ( fgets(line, 4096, in) ) {
		int cmdnr;

		tag[i=0] = strtok(line, "\t \r\n");
		while ( tag[i] && i < 31 )
			tag[++i] = strtok(0, "\t \r\n");
		while ( i < 32 )
			tag[i++] = "";

		if ( !tag[0][0] ) continue;

		for (i=0; i<32; i++)
			tag[i] = strdup(url_decode(tag[i]));

		for (cmdnr=0; cmdtab[cmdnr].text; cmdnr++)
			if ( !strcasecmp(cmdtab[cmdnr].text, tag[0]) ) break;

		if ( !cmdtab[cmdnr].text ) {
			cmd_error = "Unkown command!";
			goto abort_cmd;
		}

		cmd_error = 0;

		if ( cmdtab[cmdnr].check_perm &&
				csync_perm(tag[2], tag[1]) ) {
			cmd_error = "Wrong key or filename!";
			goto abort_cmd;
		}

		if ( cmdtab[cmdnr].check_dirty &&
				csync_check_dirty(tag[2]) ) goto abort_cmd;

		if ( cmdtab[cmdnr].unlink )
				csync_unlink(tag[2], cmdtab[cmdnr].unlink);

		switch ( cmdtab[cmdnr].action )
		{
		case A_SIG:
			{
				struct stat st;

				if ( lstat(tag[2], &st) != 0 ) {
					if ( errno == ENOENT )
						fprintf(out, "OK (not_found).\n---\noctet-stream 0\n");
					else
						cmd_error = strerror(errno);
					break;
				}
				fprintf(out, "OK (data_follows).\n");
				fprintf(out, "%s\n", csync_genchecktxt(&st, tag[2], 1));

				if ( S_ISREG(st.st_mode) )
					csync_rs_sig(tag[2], out);
				else
					fprintf(out, "octet-stream 0\n");
			}
			break;
		case A_FLUSH:
			SQL("Flushing dirty entry (if any) for file",
				"DELETE FROM dirty WHERE filename = '%s'",
				url_encode(tag[2]));
			break;
		case A_DEL:
			csync_unlink(tag[2], 0);
			break;
		case A_PATCH:
			fprintf(out, "OK (send_data).\n"); fflush(out);
			csync_rs_sig(tag[2], out);
			csync_rs_patch(tag[2], in);
			break;
		case A_MKDIR:
			/* ignore errors on creating directories if the
			 * directory does exist already. we don't need such
			 * a check for the other file types, because they
			 * will simply be unlinked if already present.
			 */
			if ( mkdir(tag[2], 0700) ) {
				struct stat st;
				if ( lstat(tag[2], &st) != 0 ||
						!S_ISDIR(st.st_mode))
					cmd_error = strerror(errno);
			}
			break;
		case A_MKCHR:
			if ( mknod(tag[2], 0700|S_IFCHR, atoi(tag[3])) )
				cmd_error = strerror(errno);
			break;
		case A_MKBLK:
			if ( mknod(tag[2], 0700|S_IFBLK, atoi(tag[3])) )
				cmd_error = strerror(errno);
			break;
		case A_MKFIFO:
			if ( mknod(tag[2], 0700|S_IFIFO, 0) )
				cmd_error = strerror(errno);
			break;
		case A_MKLINK:
			if ( symlink(tag[3], tag[2]) )
				cmd_error = strerror(errno);
			break;
		case A_MKSOCK:
			/* just ignore socket files */
			break;
		case A_SETOWN:
			if ( lchown(tag[2], atoi(tag[3]), atoi(tag[4])) )
				cmd_error = strerror(errno);
			break;
		case A_SETMOD:
			if ( chmod(tag[2], atoi(tag[3])) )
				cmd_error = strerror(errno);
			break;
		case A_SETIME:
			{
				struct utimbuf utb;
				utb.actime = atoll(tag[3]);
				utb.modtime = atoll(tag[3]);
				if ( utime(tag[2], &utb) )
					cmd_error = strerror(errno);
			}
			break;
		case A_DEBUG:
			csync_debug_out = stdout;
			break;
		case A_BYE:
			for (i=0; i<32; i++)
				tag[i] = strdup(url_decode(tag[i]));
			fprintf(out, "OK (cu_later).\n"); fflush(out);
			return;
		}

		if ( cmdtab[cmdnr].update )
			csync_file_update(tag[2]);

abort_cmd:
		if ( cmd_error )
			fprintf(out, "%s\n", cmd_error);
		else
			fprintf(out, "OK (cmd_finished).\n");
		fflush(out);

		for (i=0; i<32; i++)
			tag[i] = strdup(url_decode(tag[i]));
	}
}

