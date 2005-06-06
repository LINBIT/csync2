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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char **argv)
{
	char myhostname[256];
	char *dbname;
	int i;

	gethostname(myhostname, 256);
	myhostname[255] = 0;

	asprintf(&dbname, "%s.db", myhostname);

	printf("\n");
	printf("**********************************************************\n");
	printf("\n");
	printf("Csync2 Monitor\n");
	printf("\n");
	printf("Basedir:  %s\n", TRGDIR);
	printf("Hostname: %s\n", myhostname);
	printf("Database: %s\n", dbname);
	printf("\n");
	printf("**********************************************************\n");
	printf("\n");
	fflush(stdout);

	if (chdir(TRGDIR) < 0)
		goto io_error;

	for (i=1; i<argc; i++) {
		printf("Starting hintd for '%s' ...\n", argv[i]);
		fflush(stdout);
		if (!fork()) {
			while (1) {
				pid_t pid;
				if (!(pid=fork())) {
					execl("./cs2_hintd_win32.exe", "cs2_hintd_win32.exe",
							dbname, argv[i], NULL);
					goto io_error;
				}
				if (waitpid(pid, NULL, 0) == -1)
					goto io_error;
			}
		}
	}

	printf("Starting local csync2 daemon (listener) ...\n");
	fflush(stdout);
	if (!fork()) {
		while (1) {
			pid_t pid;
			if (!(pid=fork())) {
				execl("./csync2.exe", "csync2.exe", "-iiv", NULL);
				goto io_error;
			}
			if (waitpid(pid, NULL, 0) == -1)
				goto io_error;
		}
	}

	printf("Initial full check run ...\n");
	fflush(stdout);

	{
		pid_t pid;
		if (!(pid=fork())) {
			execl("./csync2.exe", "csync2.exe", "-crv", "/", NULL);
			goto io_error;
		}
		if (waitpid(pid, NULL, 0) == -1)
			goto io_error;
	}

	while (1) {
		pid_t pid;

		printf("[%ld] Csync2 Monitor: Checking and syncing..\n",
				(long)time(0));
		fflush(stdout);

		if (!(pid=fork())) {
			execl("./csync2.exe", "csync2.exe", "-cv", NULL);
			goto io_error;
		}
		if (waitpid(pid, NULL, 0) == -1)
			goto io_error;

		if (!(pid=fork())) {
			execl("./csync2.exe", "csync2.exe", "-uv", NULL);
			goto io_error;
		}
		if (waitpid(pid, NULL, 0) == -1)
			goto io_error;

		sleep(5);
	}

	/* never reached */
	return 0;

io_error:
	fprintf(stderr, "Csync2 Monitor I/O Error: %s\n", strerror(errno));
	return 1;
}

