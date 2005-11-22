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
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

long csync_last_printtime = 0;
FILE *csync_timestamp_out = 0;
int csync_messages_printed = 0;

time_t csync_startup_time = 0;

void csync_printtime()
{
	if (csync_timestamps || csync_timestamp_out)
	{
		time_t now = time(0);
		char ftbuffer[128];

		if (!csync_startup_time)
			csync_startup_time = now;

		if (csync_last_printtime+300 < now) {
			csync_last_printtime = now;

			strftime(ftbuffer, 128, "%Y-%m-%d %H:%M:%S %Z (GMT%z)", localtime(&now));

			if (csync_timestamp_out)
				fprintf(csync_timestamp_out, "<%d> TIMESTAMP: %s\n", (int)getpid(), ftbuffer);

			if (csync_timestamps) {
				if (csync_server_child_pid)
					fprintf(csync_debug_out, "<%d> ", csync_server_child_pid);
				fprintf(csync_debug_out, "TIMESTAMP: %s\n", ftbuffer);
			}
		}
	}
}

void csync_printtotaltime()
{
	if (csync_timestamps || csync_timestamp_out)
	{
		time_t now = time(0);
		int seconds = now - csync_startup_time;

		csync_last_printtime = 0;
		csync_printtime();

		if (csync_timestamp_out)
			fprintf(csync_timestamp_out, "<%d> TOTALTIME: %d:%02d:%02d\n",
				(int)getpid(), seconds / (60*60), (seconds/60) % 60, seconds % 60);

		if (csync_timestamps) {
			if (csync_server_child_pid)
				fprintf(csync_debug_out, "<%d> ", csync_server_child_pid);
			fprintf(csync_debug_out, "TOTALTIME: %d:%02d:%02d\n",
				seconds / (60*60), (seconds/60) % 60, seconds % 60);
		}
	}
}

void csync_printtime_prefix()
{
	time_t now = time(0);
	char ftbuffer[32];
	strftime(ftbuffer, 32, "%H:%M:%S", localtime(&now));
	fprintf(csync_debug_out, "[%s] ", ftbuffer);
}

void csync_fatal(const char *fmt, ...)
{
	va_list ap;

	if (csync_timestamps)
		csync_printtime_prefix();

	if (csync_server_child_pid)
		fprintf(csync_debug_out, "<%d> ", csync_server_child_pid);

	va_start(ap, fmt);
	vfprintf(csync_debug_out, fmt, ap);
	va_end(ap);

	csync_db_close();

	csync_last_printtime = 0;
	csync_printtime();

	exit(1);
}

void csync_debug(int lv, const char *fmt, ...)
{
	va_list ap;

	csync_printtime();

	if ( csync_debug_level < lv ) return;

	if (csync_timestamps)
		csync_printtime_prefix();

	if ( csync_server_child_pid )
		fprintf(csync_debug_out, "<%d> ", csync_server_child_pid);

	va_start(ap, fmt);
	vfprintf(csync_debug_out, fmt, ap);
	va_end(ap);

	csync_messages_printed++;
}

