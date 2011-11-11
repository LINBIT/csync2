/*
 *  csync2 - cluster synchronization tool, 2nd generation
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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

void csync_schedule_commands(const char *filename, int islocal)
{
	const struct csync_group *g = NULL;
	const struct csync_group_action *a = NULL;
	const struct csync_group_action_pattern *p = NULL;
	const struct csync_group_action_command *c = NULL;

	while ( (g=csync_find_next(g, filename)) ) {
		for (a=g->action; a; a=a->next) {
			if ( !islocal && a->do_local_only )
				continue;
			if ( islocal && !a->do_local )
				continue;
			if (!a->pattern)
				goto found_matching_pattern;
			for (p=a->pattern; p; p=p->next) {
				int fnm_pathname = p->star_matches_slashes ? 0 : FNM_PATHNAME;
				if ( !fnmatch(p->pattern, filename,
						FNM_LEADING_DIR|fnm_pathname) )
					goto found_matching_pattern;
			}
			continue;
found_matching_pattern:
			for (c=a->command; c; c=c->next)
				SQL("Add action to database",
					"INSERT INTO action (filename, command, logfile) "
					"VALUES ('%s', '%s', '%s')", url_encode(filename),
					url_encode(c->command), url_encode(a->logfile));
		}
	}
}

void csync_run_single_command(const char *command, const char *logfile)
{
	char *command_clr = strdup(url_decode(command));
	char *logfile_clr = strdup(url_decode(logfile));
	char *real_command, *mark;
	struct textlist *tl = 0, *t;
	pid_t pid;

	SQL_BEGIN("Checking for removed files",
			"SELECT filename from action WHERE command = '%s' "
			"and logfile = '%s'", command, logfile)
	{
		textlist_add(&tl, SQL_V(0), 0);
	} SQL_END;

	mark = strstr(command_clr, "%%");
	if ( !mark ) {
		real_command = strdup(command_clr);
	} else {
		int len = strlen(command_clr) + 10;
		char *pos;

		for (t = tl; t != 0; t = t->next)
			len += strlen(t->value) + 1;

		pos = real_command = malloc(len);
		memcpy(real_command, command_clr, mark-command_clr);
		real_command[mark-command_clr] = 0;

		for (t = tl; t != 0; t = t->next) {
			pos += strlen(pos);
			if ( t != tl ) *(pos++) = ' ';
			strcpy(pos, t->value);
		}

		pos += strlen(pos);
		strcpy(pos, mark+2);

		assert(strlen(real_command)+1 < len);
	}

	csync_debug(1, "Running '%s' ...\n", real_command);

	pid = fork();
	if ( !pid ) {
		close(0); close(1); close(2);
		/* 0 */ open("/dev/null", O_RDONLY);
		/* 1 */ open(logfile_clr, O_WRONLY|O_CREAT|O_APPEND, 0666);
		/* 2 */ open(logfile_clr, O_WRONLY|O_CREAT|O_APPEND, 0666);

		execl("/bin/sh", "sh", "-c", real_command, NULL);
		_exit(127);
	}

	if ( waitpid(pid, 0, 0) < 0 )
		csync_fatal("ERROR: Waitpid returned error %s.\n", strerror(errno));

	for (t = tl; t != 0; t = t->next)
		SQL("Remove action entry",
				"DELETE FROM action WHERE command = '%s' "
				"and logfile = '%s' and filename = '%s'",
				command, logfile, t->value);

	textlist_free(tl);
}

void csync_run_commands()
{
	struct textlist *tl = 0, *t;

	SQL_BEGIN("Checking for sceduled commands",
			"SELECT command, logfile FROM action GROUP BY command, logfile")
	{
		textlist_add2(&tl, SQL_V(0), SQL_V(1), 0);
	} SQL_END;

	for (t = tl; t != 0; t = t->next)
		csync_run_single_command(t->value, t->value2);

	textlist_free(tl);
}

