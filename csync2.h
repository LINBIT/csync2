/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2004, 2005, 2006  Clifford Wolf <clifford@clifford.at>
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

#define CSYNC2_VERSION "2.0-rc1"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>


#define DB_SCHEMA_VERSION 0

/* asprintf with test for no memory */

#define ASPRINTF(s, fmt, ...) do {\
	int __ret = asprintf(s, fmt, ##__VA_ARGS__);\
	if (__ret < 0) \
		csync_fatal("Out of memory in asprintf at %s:%d\n", __FILE__, __LINE__);\
} while (0)


#define VASPRINTF(s, fmt, args) do {\
	int __ret = vasprintf(s, fmt, args);\
	if (__ret < 0) \
		csync_fatal("Out of memory in vasprintf at %s:%d\n", __FILE__, __LINE__);\
} while (0)


/* action.c */

extern void csync_schedule_commands(const char *filename, int islocal);
extern int csync_check_pure(const char *filename);
extern void csync_run_commands();


/* groups.c */

struct peer {
	const char *myname;
	const char *peername;
};

extern const struct csync_group *csync_find_next(const struct csync_group *g, const char *file);
extern int csync_match_file(const char *file);
extern void csync_check_usefullness(const char *file, int recursive);
extern int csync_match_file_host(const char *file, const char *myname, const char *peername, const char **keys);
extern struct peer *csync_find_peers(const char *file, const char *thispeer);
extern const char *csync_key(const char *hostname, const char *filename);
extern int csync_perm(const char *filename, const char *key, const char *hostname);


/* error.c */

extern void csync_printtime();
extern void csync_printtotaltime();
extern void csync_fatal(const char *fmt, ...);
extern void csync_debug(int lv, const char *fmt, ...);

#define csync_debug_ping(N) \
csync_debug(N, "--> %s %d\n", __FILE__, __LINE__)


/* conn.c */

extern int conn_open(const char *peername);
extern int conn_set(int infd, int outfd);
extern int conn_activate_ssl(int server_role);
extern int conn_check_peer_cert(const char *peername, int callfatal);
extern int conn_close();

extern int conn_read(void *buf, size_t count);
extern int conn_write(const void *buf, size_t count);

extern void conn_printf(const char *fmt, ...);
extern int conn_fgets(char *s, int size);
extern size_t conn_gets(char *s, size_t size);


/* db.c */

extern void csync_db_open(const char *file);
extern void csync_db_close();

extern void csync_db_sql(const char *err, const char *fmt, ...);
extern void* csync_db_begin(const char *err, const char *fmt, ...);
extern int csync_db_next(void *vmx, const char *err,
		int *pN, const char ***pazValue, const char ***pazColName);
extern void csync_db_fin(void *vmx, const char *err);
extern const void * csync_db_colblob(void *stmtx,int col);
extern char *db_default_database(char *dbdir, char *myhostname, char *cfg_name);


#define SQL(e, s, ...) csync_db_sql(e, s, ##__VA_ARGS__)

#if 0
#if defined(HAVE_LIBSQLITE)
#define SQL_BEGIN(e, s, ...) \
{ \
	char *SQL_ERR = e; \
	void *SQL_VM = csync_db_begin(SQL_ERR, s, ##__VA_ARGS__); \
	int SQL_COUNT = 0; \
	while (1) { \
		const char **dataSQL_V, **dataSQL_N; \
		int SQL_C; \
		if ( !csync_db_next(SQL_VM, SQL_ERR, \
					&SQL_C, &dataSQL_V, &dataSQL_N) ) break; \
		SQL_COUNT++;

#define SQL_V(col) \
	(dataSQL_V[(col)])
#endif
#endif

// #if defined(HAVE_LIBSQLITE3)

#define SQL_BEGIN(e, s, ...) \
{ \
	char *SQL_ERR = e; \
	void *SQL_VM = csync_db_begin(SQL_ERR, s, ##__VA_ARGS__); \
	int SQL_COUNT = 0; \
\
	if (SQL_VM) { \
		while (1) { \
			const char **dataSQL_V, **dataSQL_N; \
			int SQL_C; \
			if ( !csync_db_next(SQL_VM, SQL_ERR, \
						&SQL_C, &dataSQL_V, &dataSQL_N) ) break; \
			SQL_COUNT++;

#define SQL_V(col) \
	(csync_db_colblob(SQL_VM,(col)))
// #endif
#define SQL_FIN }{

#define SQL_END \
		} \
		csync_db_fin(SQL_VM, SQL_ERR); \
	} \
}

extern int db_blocking_mode;
extern int db_sync_mode;


/* rsync.c */

extern int csync_rs_check(const char *filename, int isreg);
extern void csync_rs_sig(const char *filename);
extern int csync_rs_delta(const char *filename);
extern int csync_rs_patch(const char *filename);


/* checktxt.c */

extern const char *csync_genchecktxt(const struct stat *st, const char *filename, int ign_mtime);
extern int csync_cmpchecktxt(const char *a, const char *b);


/* check.c */

extern void csync_hint(const char *file, int recursive);
extern void csync_check(const char *filename, int recursive, int init_run);
extern void csync_mark(const char *file, const char *thispeer, const char *peerfilter);


/* update.c */

extern void csync_update(const char **patlist, int patnum, int recursive, int dry_run);
extern int csync_diff(const char *myname, const char *peername, const char *filename);
extern int csync_insynctest(const char *myname, const char *peername, int init_run, int auto_diff, const char *filename);
extern int csync_insynctest_all(int init_run, int auto_diff, const char *filename);
extern void csync_remove_old();


/* daemon.c */

extern void csync_daemon_session();
extern int csync_copy_file(int fd_in, int fd_out);


/* getrealfn.c */

extern char *getrealfn(const char *filename);


/* urlencode.c */

/* only use this functions if you understood the sideeffects of the ringbuffer
 * used to allocate the return values.
 */
const char *url_encode(const char *in);
const char *url_decode(const char *in);


/* prefixsubst.c */

/* another ringbuffer here. so use it with care!! */
const char *prefixsubst(const char *in);
const char *prefixencode(const char *filename);


/* textlist implementation */

struct textlist;

struct textlist {
	struct textlist *next;
	int intvalue;
	char *value;
	char *value2;
};

static inline void textlist_add(struct textlist **listhandle, const char *item, int intitem)
{
	struct textlist *tmp = *listhandle;
	*listhandle = malloc(sizeof(struct textlist));
	(*listhandle)->intvalue = intitem;
	(*listhandle)->value = strdup(item);
	(*listhandle)->value2 = 0;
	(*listhandle)->next = tmp;
}

static inline void textlist_add2(struct textlist **listhandle, const char *item, const char *item2, int intitem)
{
	struct textlist *tmp = *listhandle;
	*listhandle = malloc(sizeof(struct textlist));
	(*listhandle)->intvalue = intitem;
	(*listhandle)->value = strdup(item);
	(*listhandle)->value2 = strdup(item2);
	(*listhandle)->next = tmp;
}

static inline void textlist_free(struct textlist *listhandle)
{
	struct textlist *next;
	while (listhandle != 0) {
		next = listhandle->next;
		free(listhandle->value);
		if ( listhandle->value2 )
			free(listhandle->value2);
		free(listhandle);
		listhandle = next;
	}
}


/* config structures */

struct csync_nossl;
struct csync_group;
struct csync_group_host;
struct csync_group_pattern;

struct csync_group_host {
	struct csync_group_host *next;
	const char *hostname;
	int on_left_side;
	int slave;
};

struct csync_group_pattern {
	struct csync_group_pattern *next;
	int isinclude, iscompare, star_matches_slashes;
	const char *pattern;
};

struct csync_group_action_pattern {
	struct csync_group_action_pattern *next;
	int star_matches_slashes;
	const char *pattern;
};

struct csync_group_action_command {
	struct csync_group_action_command *next;
	const char *command;
};

struct csync_group_action {
	struct csync_group_action *next;
	struct csync_group_action_pattern *pattern;
	struct csync_group_action_command *command;
	const char *logfile;
	int do_local;
	int do_local_only;
};

struct csync_group {
	struct csync_group *next;
	struct csync_group_host *host;
	struct csync_group_pattern *pattern;
	struct csync_group_action *action;
	const char *key, *myname, *gname;
	int auto_method, local_slave;
	const char *backup_directory;
	int backup_generations;
	int hasactivepeers;
};

struct csync_prefix {
	const char *name, *path;
	struct csync_prefix *next;
};

struct csync_nossl {
	struct csync_nossl *next;
	const char *pattern_from;
	const char *pattern_to;
};

enum CSYNC_AUTO_METHOD {
	CSYNC_AUTO_METHOD_NONE,
	CSYNC_AUTO_METHOD_FIRST,

	CSYNC_AUTO_METHOD_YOUNGER,
	CSYNC_AUTO_METHOD_OLDER,

	CSYNC_AUTO_METHOD_BIGGER,
	CSYNC_AUTO_METHOD_SMALLER,

	CSYNC_AUTO_METHOD_LEFT,
	CSYNC_AUTO_METHOD_RIGHT,

	CSYNC_AUTO_METHOD_LEFT_RIGHT_LOST
};


/* global variables */

extern struct csync_group  *csync_group;
extern struct csync_prefix *csync_prefix;
extern struct csync_nossl  *csync_nossl;

extern unsigned csync_lock_timeout;
extern char *csync_tempdir;

extern char *csync_database;

extern int csync_error_count;
extern int csync_debug_level;
extern int csync_syslog;
extern FILE *csync_debug_out;

extern long csync_last_printtime;
extern FILE *csync_timestamp_out;

extern int csync_messages_printed;
extern int csync_server_child_pid;
extern int csync_timestamps;
extern int csync_new_force;

extern char myhostname[];
extern char *csync_port;
extern char *active_grouplist;
extern char *active_peerlist;

extern char *cfgname;

extern int csync_ignore_uid;
extern int csync_ignore_gid;
extern int csync_ignore_mod;

extern int csync_dump_dir_fd;

extern int csync_compare_mode;

#ifdef HAVE_LIBGNUTLS
extern int csync_conn_usessl;
#endif

#ifdef __CYGWIN__
extern int csync_lowercyg_disable;
extern int csync_lowercyg_used;
extern int csync_cygwin_case_check(const char *filename);
#endif

static inline int lstat_strict(const char *filename, struct stat *buf) {
#ifdef __CYGWIN__
	if (csync_lowercyg_disable && !csync_cygwin_case_check(filename)) {
		errno = ENOENT;
		return -1;
	}
#endif
	return lstat(filename, buf);
}

static inline char *on_cygwin_lowercase(char *s) {
#ifdef __CYGWIN__
	if (!csync_lowercyg_disable) {
		int i;
		for (i=0; s[i]; i++)
			s[i] = tolower(s[i]);
	}
	csync_lowercyg_used = 1;
#endif
	return s;
}

#endif /* CSYNC2_H */

