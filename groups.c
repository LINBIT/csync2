#include "csync2.h"
#include <fnmatch.h>

int match_pattern_list(
		const char *filename, const char *basename,
		const struct csync_group_pattern *p)
{
	int match_path = 0, match_base = 1;

	while (p) {
		int matched = 0;
		if ( p->pattern[0] != '/' ) {
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
		if ( ! g->hasme ) continue;
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
		if ( ! g->hasme ) continue;
		for (p=g->pattern; p; p=p->next)
			if ( p->pattern[0] == '/' && p->isinclude ) {
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

const char **csync_find_hosts(const char *file)
{
	const struct csync_group *g = NULL;
	const char **hlist = 0;
	int hl_size = 0;

	while ( (g=csync_find_next(g, file)) ) {
		struct csync_group_host *h = g->host;

		while (h) {
			int i=0;
			while (hlist && hlist[i])
				if ( !strcmp(hlist[i++], h->hostname) )
						goto next_host;
			hlist = realloc(hlist, sizeof(char*)*(++hl_size+1));
			hlist[hl_size-1] = h->hostname;
			hlist[hl_size] = 0;
next_host:
			h = h->next;
		}
	}

	return hlist;
}

const char *csync_key(const char *hostname, const char *filename)
{
	const struct csync_group *g = NULL;
	struct csync_group_host *h;

	while ( (g=csync_find_next(g, filename)) )
		for (h = g->host; h; h = h->next)
			if (!strcmp(h->hostname,hostname)) return g->key;

	return 0;
}

int csync_perm(const char * filename, const char * key)
{
	const struct csync_group *g = NULL;

	while ( (g=csync_find_next(g, filename)) )
		if ( !strcmp(g->key, key) ) return 0;

	return 1;
}

