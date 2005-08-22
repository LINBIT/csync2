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
#include <fnmatch.h>

int match_pattern_list(
		const char *filename, const char *basename,
		const struct csync_group_pattern *p)
{
	int match_path = 0, match_base = 1;

	while (p) {
		int matched = 0;
		if ( p->pattern[0] != '/' && p->pattern[0] != '%' ) {
			if ( !fnmatch(p->pattern, basename, 0) ) {
				match_base = p->isinclude;
				matched = 1;
			}
		} else {
			if ( !fnmatch(p->pattern, filename,
					FNM_LEADING_DIR|FNM_PATHNAME) ) {
				match_path = p->isinclude;
				matched = 1;
			}
		}
		if ( matched ) {
			csync_debug(2, "Match (%c): %s on %s\n",
				p->isinclude ? '+' : '-',
				p->pattern, filename);
		}
		p = p->next;
	}

	return match_path && match_base;
}

const struct csync_group *csync_find_next(
		const struct csync_group *g, const char *file)
{
	const char * basename = strrchr(file, '/');

	if ( basename ) basename++;
	else basename = file;

	for (g = g==0 ? csync_group : g->next;  g;  g = g->next) {
		if ( ! g->myname ) continue;
		if ( match_pattern_list(file, basename, g->pattern) ) break;
	}

	return g;
}

int csync_step_into(const char *file)
{
	const struct csync_group_pattern *p;
	const struct csync_group *g;

	if ( !strcmp(file, "/") ) return 1;

	for (g=csync_group; g; g=g->next) {
		if ( ! g->myname ) continue;
		for (p=g->pattern; p; p=p->next)
			if ( (p->pattern[0] == '/' || p->pattern[0] == '%') && p->isinclude ) {
				char t[strlen(p->pattern)+1], *l;
				strcpy(t, p->pattern);
				while ( (l=strrchr(t, '/')) != 0 ) {
					*l = 0;
					if ( !fnmatch(t, file, FNM_PATHNAME) )
								return 1;
				}
			}
	}

	return 0;
}

int csync_match_file(const char *file)
{
	if ( csync_find_next(0, file) ) return 2;
	if ( csync_step_into(file) ) return 1;
	return 0;
}

void csync_check_usefullness(const char *file, int recursive)
{
	if ( csync_find_next(0, file) ) return;
	if ( recursive && csync_step_into(file) ) return;
	csync_debug(0, "WARNING: Parameter will be ignored: %s\n", file);
}

int csync_match_file_host(const char *file, const char *myname, const char *peername, const char **keys)
{
	const struct csync_group *g = NULL;

	while ( (g=csync_find_next(g, file)) ) {
		struct csync_group_host *h = g->host;
		if ( strcmp(myname, g->myname) ) continue;
		if (keys) {
			const char **k = keys;
			while (*k && **k)
				if ( !strcmp(*(k++), g->key) ) goto found_key;
			continue;
		}
found_key:
		while (h) {
			if ( !strcmp(h->hostname, peername) ) return 1;
			h = h->next;
		}
	}

	return 0;
}

struct peer *csync_find_peers(const char *file, const char *thispeer)
{
	const struct csync_group *g = NULL;
	struct peer *plist = 0;
	int pl_size = 0;

	while ( (g=csync_find_next(g, file)) ) {
		struct csync_group_host *h = g->host;

		if (thispeer) {
			while (h) {
				if ( !strcmp(h->hostname, thispeer) )
					goto next_group;
				h = h->next;
			}
			h = g->host;
		}

		while (h) {
			int i=0;
			while (plist && plist[i].peername)
				if ( !strcmp(plist[i++].peername, h->hostname) )
						goto next_host;
			plist = realloc(plist, sizeof(struct peer)*(++pl_size+1));
			plist[pl_size-1].peername = h->hostname;
			plist[pl_size-1].myname = g->myname;
			plist[pl_size].peername = 0;
next_host:
			h = h->next;
		}
next_group:	;
	}

	return plist;
}

const char *csync_key(const char *hostname, const char *filename)
{
	const struct csync_group *g = NULL;
	struct csync_group_host *h;

	while ( (g=csync_find_next(g, filename)) )
		for (h = g->host; h; h = h->next)
			if (!strcmp(h->hostname, hostname)) return g->key;

	return 0;
}

int csync_perm(const char * filename, const char * key, const char * hostname)
{
	const struct csync_group *g = NULL;
	struct csync_group_host *h;

	while ( (g=csync_find_next(g, filename)) ) {
		if ( hostname != 0 ) {
			for (h = g->host; h; h = h->next)
				if (!strcmp(h->hostname, hostname)) goto found_host;
			continue;
		}
found_host:
		if ( !strcmp(g->key, key) ) return 0;
	}

	return 1;
}

