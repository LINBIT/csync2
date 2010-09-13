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

#include "csync2.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "db_api.h"

#define DEADLOCK_MESSAGE \
	"Database backend is exceedingly busy => Terminating (requesting retry).\n"

int db_blocking_mode = 1;
int db_sync_mode = 1;

extern int db_type; 
static db_conn_p db = 0;
// TODO make configurable
int wait = 1;

static int get_dblock_timeout()
{
	return getpid() % 7 + csync_lock_timeout;
}

static int tqueries_counter = -50;
static time_t transaction_begin = 0;
static time_t last_wait_cycle = 0;
static int begin_commit_recursion = 0;
static int in_sql_query = 0;

void csync_db_alarmhandler(int signum)
{
	if ( in_sql_query || begin_commit_recursion )
		alarm(2);

	if (tqueries_counter <= 0)
		return;

	begin_commit_recursion++;

	csync_debug(2, "Database idle in transaction. Forcing COMMIT.\n");
	SQL("COMMIT ", "COMMIT ");
	tqueries_counter = -10;

	begin_commit_recursion--;
}

void csync_db_maybegin()
{
	if ( !db_blocking_mode || begin_commit_recursion ) return;
	begin_commit_recursion++;

	signal(SIGALRM, SIG_IGN);
	alarm(0);

	tqueries_counter++;
	if (tqueries_counter <= 0) {
		begin_commit_recursion--;
		return;
	}

	if (tqueries_counter == 1) {
		transaction_begin = time(0);
		if (!last_wait_cycle)
			last_wait_cycle = transaction_begin;
		SQL("BEGIN ", "BEGIN ");
	}

	begin_commit_recursion--;
}

void csync_db_maycommit()
{
	time_t now;

	if ( !db_blocking_mode || begin_commit_recursion ) return;
	begin_commit_recursion++;

	if (tqueries_counter <= 0) {
		begin_commit_recursion--;
		return;
	}

	now = time(0);

	if ((now - last_wait_cycle) > 10) {
		SQL("COMMIT", "COMMIT ");
		if (wait) {
		  csync_debug(2, "Waiting %d secs so others can lock the database (%d - %d)...\n", wait, (int)now, (int)last_wait_cycle);
		  sleep(wait);
		}
		last_wait_cycle = 0;
		tqueries_counter = -10;
		begin_commit_recursion--;
		return;
	}

	if ((tqueries_counter > 1000) || ((now - transaction_begin) > 3)) {
	        SQL("COMMIT ", "COMMIT ");
		tqueries_counter = 0;
		begin_commit_recursion--;
		return;
	}

	signal(SIGALRM, csync_db_alarmhandler);
	alarm(10);

	begin_commit_recursion--;
	return;
}

void csync_db_open(const char *file)
{
        int rc = db_open(file, db_type, &db);
	if ( rc != DB_OK )
		csync_fatal("Can't open database: %s\n", file);
	db_set_logger(db, csync_debug);

	/* ignore errors on table creation */
	in_sql_query++;

	if (db_schema_version(db) < 0)
		db_upgrade_to_schema(db, 0);

	if (!db_sync_mode)
		db_exec(db, "PRAGMA synchronous = OFF");
	in_sql_query--;
	// return db;
}

void csync_db_close()
{
	if (!db || begin_commit_recursion) return;

	begin_commit_recursion++;
	if (tqueries_counter > 0) {
	        SQL("COMMIT ", "COMMIT ");
		tqueries_counter = -10;
	}
	db_close(db);
	begin_commit_recursion--;
	db = 0;
}

void csync_db_sql(const char *err, const char *fmt, ...)
{
	char *sql;
	va_list ap;
	int rc, busyc = 0;

	va_start(ap, fmt);
	vasprintf(&sql, fmt, ap);
	va_end(ap);

	in_sql_query++;
	csync_db_maybegin();

	csync_debug(2, "SQL: %s\n", sql);

	while (1) {
	  rc = db_exec(db, sql);
	  if ( rc != DB_BUSY ) break;
	  if (busyc++ > get_dblock_timeout()) { db = 0; csync_fatal(DEADLOCK_MESSAGE); }
	  csync_debug(2, "Database is busy, sleeping a sec.\n");
	  sleep(1);
	}

	if ( rc != DB_OK && err )
		csync_fatal("Database Error: %s [%d]: %s on executing %s\n", err, rc, db_errmsg(db), sql);
	free(sql);

	csync_db_maycommit();
	in_sql_query--;
}

void* csync_db_begin(const char *err, const char *fmt, ...)
{
	db_stmt_p stmt;
	char *sql;
	va_list ap;
	int rc, busyc = 0;
	char *ppTail; 
	va_start(ap, fmt);
	vasprintf(&sql, fmt, ap);
	va_end(ap);

	in_sql_query++;
	csync_db_maybegin();

	csync_debug(2, "SQL: %s\n", sql);
	while (1) {
	        rc = db_prepare_stmt(db, sql, &stmt, &ppTail);
		if ( rc != DB_BUSY ) break;
		if (busyc++ > get_dblock_timeout()) { db = 0; csync_fatal(DEADLOCK_MESSAGE); }
		csync_debug(2, "Database is busy, sleeping a sec.\n");
		sleep(1);
	}

	if ( rc != DB_OK && err )
		csync_fatal("Database Error: %s [%d]: %s on executing %s\n", err, rc, db_errmsg(db), sql);
	free(sql);

	return stmt;
}

const char *csync_db_get_column_text(void  *stmt, int column) {
	return db_stmt_get_column_text(stmt, column);
}

int csync_db_get_column_int(void *stmt, int column) {
  return db_stmt_get_column_int((db_stmt_p) stmt, column);
}

int csync_db_next(void *vmx, const char *err,
		int *pN, const char ***pazValue, const char ***pazColName)
{
	db_stmt_p stmt = vmx;
	int rc, busyc = 0;

	csync_debug(4, "Trying to fetch a row from the database.\n");

	while (1) {
		rc = db_stmt_next(stmt);
		if ( rc != DB_BUSY ) 
		  break;
		if (busyc++ > get_dblock_timeout()) { 
		  db = 0; 
		  csync_fatal(DEADLOCK_MESSAGE); 
		}
		csync_debug(2, "Database is busy, sleeping a sec.\n");
		sleep(1);
	}

	if ( rc != DB_OK && rc != DB_ROW &&
	     rc != DB_DONE && err )
		csync_fatal("Database Error: %s [%d]: %s\n", err, rc, db_errmsg(db));

	return rc == DB_ROW;
}

#if defined(HAVE_LIBSQLITE3)
const void * csync_db_colblob(void *stmtx, int col) {
       db_stmt_p stmt = stmtx;
       const void *ptr = stmt->get_column_blob(stmt, col);
       if (stmt->db && stmt->db->logger) {
	 stmt->db->logger(4, "DB get blob: %s ", (char *) ptr);
       }
       return ptr;
}
#endif

void csync_db_fin(void *vmx, const char *err)
{
        db_stmt_p stmt = (db_stmt_p) vmx;
	int rc, busyc = 0;

	if (vmx == NULL)
	   return;

	csync_debug(2, "SQL Query finished.\n");

	while (1) {
	  rc = db_stmt_close(stmt);
	  if ( rc != DB_BUSY ) 
	    break;
	  if (busyc++ > get_dblock_timeout()) { db = 0; csync_fatal(DEADLOCK_MESSAGE); }
	  csync_debug(2, "Database is busy, sleeping a sec.\n");
	  sleep(1);
	}

	if ( rc != DB_OK && err )
		csync_fatal("Database Error: %s [%d]: %s\n", err, rc, db_errmsg(db));

	csync_db_maycommit();
	in_sql_query--;
}

