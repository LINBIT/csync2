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
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void csync_fatal(const char *fmt, ...)
{
	va_list ap;

	if ( csync_server_child_pid )
		fprintf(csync_debug_out, "<%d>", csync_server_child_pid);

	va_start(ap, fmt);
	vfprintf(csync_debug_out, fmt, ap);
	va_end(ap);

	csync_db_close();
	exit(1);
}

void csync_debug(int lv, const char *fmt, ...)
{
	va_list ap;

	if ( csync_debug_level < lv ) return;

	if ( csync_server_child_pid )
		fprintf(csync_debug_out, "<%d>", csync_server_child_pid);

	va_start(ap, fmt);
	vfprintf(csync_debug_out, fmt, ap);
	va_end(ap);
}

