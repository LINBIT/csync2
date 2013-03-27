/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  Copyright (C) 2004 - 2013 LINBIT Information Technologies GmbH
 *  http://www.linbit.com; see also AUTHORS
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "csync2.h"
#include <fnmatch.h>

int csync_compare_mode = 0;

int match_pattern_list(
		const char *filename, const char *basename,
		const struct csync_group_pattern *p)
{
	int match_path = 0, match_base = 1;

	while (p) {
		int matched = 0;

		if ( p->iscompare && !csync_compare_mode )
			goto next_pattern;

		if ( p->pattern[0] != '/' && p->pattern[0] != '%' ) {
			if ( !fnmatch(p->pattern, basename, 0) ) {
				match_base = p->isinclude;
				matched = 1;
			}
		} else {
			int fnm_pathname = p->star_matches_slashes ? 0 : FNM_PATHNAME;
			if ( !fnmatch(p->pattern, filename,
					FNM_LEADING_DIR|fnm_pathname) ) {
				match_path = p->isinclude;
				matched = 1;
			}
		}
		if ( matched ) {
			csync_debug(2, "Match (%c): %s on %s\n",
				p->isinclude ? '+' : '-',
				p->pattern, filename);
		}
next_pattern:
		p = p->next;
	}

	return match_path && match_base;
}

const struct csync_group *csync_find_next(
		const struct csync_group *g, const char *file)
{
	const char *basename = strrchr(file, '/');

	if ( basename ) basename++;
	else basename = file;

	for (g = g==0 ? csync_group : g->next;  g;  g = g->next) {
		if ( !g->myname ) continue;
		if ( csync_compare_mode && !g->hasactivepeers ) continue;
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
		if ( !g->myname ) continue;
		if ( csync_compare_mode && !g->hasactivepeers ) continue;
		for (p=g->pattern; p; p=p->next) {
			if ( p->iscompare && !csync_compare_mode )
				continue;
			if ( (p->pattern[0] == '/' || p->pattern[0] == '%') && p->isinclude ) {
				char t[strlen(p->pattern)+1], *l;
				int fnm_pathname = p->star_matches_slashes ? 0 : FNM_PATHNAME;
				strcpy(t, p->pattern);
				while ( (l=strrchr(t, '/')) != 0 ) {
					*l = 0;
					if ( !fnmatch(t, file, fnm_pathname) )
								return 1;
				}
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
	struct csync_prefix *p = csync_prefix;

	if ( csync_find_next(0, file) ) return;
	if ( recursive && csync_step_into(file) ) return;

	if (*file == '/')
		while (p) {
			if (p->path) {
				int p_len = strlen(p->path);
				int f_len = strlen(file);

				/* p->path is some subtree of file */
				if (p_len > f_len && !strncmp(p->path, file, f_len) && p->path[f_len] == '/')
					return;

				/* file is somewhere below p->path */
				if (p_len <= f_len && !strncmp(p->path, file, p_len) &&
						(file[p_len] == '/' || !file[p_len])) {
					char new_file[strlen(p->name) + strlen(file+p_len) + 10];
					sprintf(new_file, "%%%s%%%s", p->name, file+p_len);

					if ( csync_find_next(0, new_file) ) return;
					if ( recursive && csync_step_into(new_file) ) return;
				}
			}
			p = p->next;
		}

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
					break;
				h = h->next;
			}
			if (!h)
				goto next_group;
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

int csync_perm(const char *filename, const char *key, const char *hostname)
{
	const struct csync_group *g = NULL;
	struct csync_group_host *h;
	int false_retcode = 1;

	while ( (g=csync_find_next(g, filename)) ) {
		if ( !hostname )
			continue;
		for (h = g->host; h; h = h->next)
			if (!strcmp(h->hostname, hostname) &&
			    !strcmp(g->key, key)) {
				if (!h->slave) return 0;
				else false_retcode = 2;
			}
	}

	return false_retcode;
}

