/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2005  Clifford Wolf <clifford@clifford.at>
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

#define RINGBUFF_LEN 10

static char *ringbuff[RINGBUFF_LEN];
static int ringbuff_counter = 0;

const char *prefixsubst(const char *in)
{
	struct csync_prefix *p;
	const char *pn, *path;
	int pn_len;

	if (!in || *in != '%')
		return in;

	pn = in+1;
	pn_len = strcspn(pn, "%");

	path = pn+pn_len;
	if (*path == '%') path++;

	for (p = csync_prefix; p; p = p->next) {
		if (strlen(p->name) == pn_len && !strncmp(p->name, pn, pn_len) && p->path) {
			ringbuff_counter = (ringbuff_counter+1) % RINGBUFF_LEN;
			if (ringbuff[ringbuff_counter])
				free(ringbuff[ringbuff_counter]);
			asprintf(&ringbuff[ringbuff_counter], "%s%s", p->path, path);
			return ringbuff[ringbuff_counter];
		}
	}

	csync_fatal("Prefix '%.*s' is not defined for host '%s'.\n",
			pn_len, pn, myhostname);
	return 0;
}

