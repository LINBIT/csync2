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

#ifndef CSYNC2_H
#define CSYNC2_H 1

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CSYNC_PORT 30865


/* groups.c */

extern int csync_match_file(const char *file);
extern const char **csync_find_hosts(const char *file);
extern const char * csync_key(const char *hostname, const char *filename);
extern int csync_perm(const char * filename, const char * key);


/* error.c */

extern void csync_fatal(const char *fmt, ...);
extern void csync_debug(int lv, const char *fmt, ...);


/* db.c */

extern void csync_db_open(const char *file);
extern void csync_db_close();

extern void csync_db_sql(const char *err, const char *fmt, ...);
extern void* csync_db_begin(const char *err, const char *fmt, ...);
extern int csync_db_next(void *vmx, const char *err,
		int *pN, const char ***pazValue, const char ***pazColName);
extern void csync_db_fin(void *vmx, const char *err);

#define SQL(e, s, ...) csync_db_sql(e, s, ##__VA_ARGS__)

#define SQL_BEGIN(e, s, ...) \
{ \
	char *SQL_ERR = e; \
	void *SQL_VM = csync_db_begin(SQL_ERR, s, ##__VA_ARGS__); \
	int SQL_COUNT = 0; \
	while (1) { \
		const char **SQL_V, **SQL_N; \
		int SQL_C; \
		if ( !csync_db_next(SQL_VM, SQL_ERR, \
					&SQL_C, &SQL_V, &SQL_N) ) break; \
		SQL_COUNT++;

#define SQL_FIN }{

#define SQL_END \
	} \
	csync_db_fin(SQL_VM, SQL_ERR); \
}


/* rsync.c */

extern int csync_rs_check(const char * filename, FILE * in_sig, int isreg);
extern void csync_rs_sig(const char * filename, FILE * out_sig);
extern void csync_rs_delta(const char * filename, FILE * in_sig, FILE * out_delta);
extern void csync_rs_patch(const char * filename, FILE * in_delta);


/* checktxt.c */

extern const char *csync_genchecktxt(const struct stat *st, const char *filename, int ign_mtime);
extern int csync_cmpchecktxt(const char *a, const char *b);


/* check.c */

extern void csync_hint(const char *file, int recursive);
extern void csync_check(const char * filename, int recursive);
extern void csync_mark(const char *file);


/* update.c */

extern void csync_update(const char **patlist, int patnum, int recursive);


/* daemon.c */

extern void csync_daemon_session(FILE * in, FILE * out);


/* getrealfn.c */

extern char *getrealfn(const char *filename);


/* urlencode.c */

/* only use this functions if you understood the sideeffects of the ringbuffer
 * used to allocate the return values.
 */
const char *url_encode(const char * in);
const char *url_decode(const char * in);


/* textlist implementation */

struct textlist;

struct textlist {
	struct textlist *next;
	int intvalue;
	char *value;
};

static inline void textlist_add(struct textlist **listhandle, const char *item, int intitem)
{
	struct textlist *tmp = *listhandle;
	*listhandle = malloc(sizeof(struct textlist));
	(*listhandle)->intvalue = intitem;
	(*listhandle)->value = strdup(item);
	(*listhandle)->next = tmp;
}

static inline void textlist_free(struct textlist *listhandle)
{
	struct textlist *next;
	while (listhandle != 0) {
		next = listhandle->next;
		free(listhandle->value);
		free(listhandle);
		listhandle = next;
	}
}


/* config structures */

struct csync_group;
struct csync_group_host;
struct csync_group_pattern;

struct csync_group_host {
	struct csync_group_host *next;
	const char *hostname;
};

struct csync_group_pattern {
	struct csync_group_pattern *next;
	int isinclude;
	const char *pattern;
};

struct csync_group {
	struct csync_group *next;
	struct csync_group_host *host;
	struct csync_group_pattern *pattern;
	const char *key;
	int hasme;
};


/* global variables */

extern struct csync_group *csync_group;

extern int csync_error_count;
extern int csync_debug_level;
extern FILE *csync_debug_out;

extern char myhostname[];


#endif /* CSYNC2_H */

