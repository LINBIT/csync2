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
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>

/*
 * this csync_genchecktxt() function might not be nice or
 * optimal - but it is hackish and easy to read at the same
 * time....  ;-)
 */

#define xxprintf(...) \
	{ char buffer; /* needed for older glibc */	\
	int t = snprintf(&buffer, 1, ##__VA_ARGS__);	\
	elements[elidx]=alloca(t+1);			\
	snprintf(elements[elidx], t+1, ##__VA_ARGS__);	\
	len+=t; elidx++; }

const char *csync_genchecktxt(const struct stat *st, const char *filename, int ign_mtime)
{
	static char *buffer = 0;
	char *elements[64];
	int elidx=0, len=1;
	int i, j, k;

	/* version 1 of this check text */
	xxprintf("v1");

	/* general data */
	if ( !S_ISLNK(st->st_mode) ) xxprintf(":mtime=%Ld",
			ign_mtime ? (long long)0 : (long long)st->st_mtime);
	xxprintf(":mode=%d:uid=%d:gid=%d",
			(int)st->st_mode, (int)st->st_uid, (int)st->st_gid);

	if ( S_ISREG(st->st_mode) )
		xxprintf(":type=reg:size=%Ld", (long long)st->st_size);

	if ( S_ISDIR(st->st_mode) )
		xxprintf(":type=dir");

	if ( S_ISCHR(st->st_mode) )
		xxprintf(":type=chr:dev=%d", (int)st->st_rdev);

	if ( S_ISBLK(st->st_mode) )
		xxprintf(":type=blk:dev=%d", (int)st->st_rdev);

	if ( S_ISFIFO(st->st_mode) )
		xxprintf(":type=fifo");

	if ( S_ISLNK(st->st_mode) ) {
		char tmp[4096];
		int r = readlink(filename, tmp, 4095);
		tmp[ r >= 0 ? r : 0 ] = 0;
		xxprintf(":type=lnk:target=%s", url_encode(tmp));
	}

	if ( S_ISSOCK(st->st_mode) )
		xxprintf(":type=sock");

	if ( buffer ) free(buffer);
	buffer = malloc(len);

	for (i=j=0; j<elidx; j++)
		for (k=0; elements[j][k]; k++)
			buffer[i++] = elements[j][k];
	assert(i == len-1);
	buffer[i]=0;

	return buffer;
}

/* In future version of csync this might also convert
 * older checktxt strings to the new format.
 */
int csync_cmpchecktxt(const char *a, const char *b)
{
	return !strcmp(a, b);
}

