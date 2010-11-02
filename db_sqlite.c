/*
 *  
 *  
 *  Copyright (C) 2010  Dennis Schafroth <dennis@schafroth.com>>
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
#if defined(HAVE_LIBSQLITE3)
#include <sqlite3.h>
#endif
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "db_api.h"
#include "db_sqlite.h"
#include "dl.h"

#ifndef HAVE_LIBSQLITE3
int db_sqlite_open(const char *file, db_conn_p *conn_p) {
  return DB_FAIL;
}
#else

static struct db_sqlite3_fns {
	int (*sqlite3_open_fn) (const char*, sqlite3 **);
	int (*sqlite3_close_fn) (sqlite3 *);
	const char *(*sqlite3_errmsg_fn) (sqlite3 *);
	int (*sqlite3_exec_fn) (sqlite3*, const char *,
		int (*) (void*,int,char**,char**), void*, char **);
	int (*sqlite3_prepare_v2_fn)(sqlite3 *, const char *, int,
		sqlite3_stmt **, const char **pzTail);
	const unsigned char *(*sqlite3_column_text_fn)(sqlite3_stmt*, int);
	const void *(*sqlite3_column_blob_fn)(sqlite3_stmt*, int);
	int (*sqlite3_column_int_fn)(sqlite3_stmt*, int);
	int (*sqlite3_step_fn)(sqlite3_stmt*);
	int (*sqlite3_finalize_fn)(sqlite3_stmt *);
} f;

static void *dl_handle;


static void db_sqlite3_dlopen(void)
{
        dl_handle = dlopen("libsqlite3.so", RTLD_LAZY);
        if (dl_handle == NULL) {
                csync_fatal("Could not open libsqlite3.so: %s\nPlease install sqlite3 client library (libsqlite3) or use other database (postgres, mysql)\n", dlerror());
        }

        LOOKUP_SYMBOL(dl_handle, sqlite3_open);
        LOOKUP_SYMBOL(dl_handle, sqlite3_close);
        LOOKUP_SYMBOL(dl_handle, sqlite3_errmsg);
        LOOKUP_SYMBOL(dl_handle, sqlite3_exec);
        LOOKUP_SYMBOL(dl_handle, sqlite3_prepare_v2);
        LOOKUP_SYMBOL(dl_handle, sqlite3_column_text);
        LOOKUP_SYMBOL(dl_handle, sqlite3_column_blob);
        LOOKUP_SYMBOL(dl_handle, sqlite3_column_int);
        LOOKUP_SYMBOL(dl_handle, sqlite3_step);
        LOOKUP_SYMBOL(dl_handle, sqlite3_finalize);
}

static int sqlite_errors[] = { SQLITE_OK, SQLITE_ERROR, SQLITE_BUSY, SQLITE_ROW, SQLITE_DONE, -1 };
static int db_errors[]     = { DB_OK,     DB_ERROR,     DB_BUSY,     DB_ROW,     DB_DONE,     -1 };

int db_sqlite_error_map(int sqlite_err) {
  int index; 
  for (index = 0; ; index++) {
    if (sqlite_errors[index] == -1)
      return DB_ERROR;
    if (sqlite_err == sqlite_errors[index])
      return db_errors[index];
  }
}

int db_sqlite_open(const char *file, db_conn_p *conn_p)
{
  sqlite3 *db;

  db_sqlite3_dlopen();

  int rc = f.sqlite3_open_fn(file, &db);
  if ( rc != SQLITE_OK ) {
    return db_sqlite_error_map(rc);
  };
  db_conn_p conn = calloc(1, sizeof(*conn));
  if (conn == NULL) {
    return DB_ERROR;
  }
  *conn_p = conn;
  conn->private = db;
  conn->close   = db_sqlite_close;
  conn->exec    = db_sqlite_exec;
  conn->prepare = db_sqlite_prepare;
  conn->errmsg  = db_sqlite_errmsg;
  conn->upgrade_to_schema = db_sqlite_upgrade_to_schema;
  return db_sqlite_error_map(rc);
}

void db_sqlite_close(db_conn_p conn)
{
  if (!conn)
    return;
  if (!conn->private) 
    return;
  f.sqlite3_close_fn(conn->private);
  conn->private = 0;
}

const char *db_sqlite_errmsg(db_conn_p conn)
{
  if (!conn)
    return "(no connection)";
  if (!conn->private)
    return "(no private data in conn)";
  return f.sqlite3_errmsg_fn(conn->private);
}

int db_sqlite_exec(db_conn_p conn, const char *sql) {
  int rc;
  if (!conn) 
    return DB_NO_CONNECTION; 

  if (!conn->private) {
    /* added error element */
    return DB_NO_CONNECTION_REAL;
  }
  rc = f.sqlite3_exec_fn(conn->private, sql, 0, 0, 0);
  return db_sqlite_error_map(rc);
}

int db_sqlite_prepare(db_conn_p conn, const char *sql, db_stmt_p *stmt_p, char **pptail) {
  int rc;

  *stmt_p = NULL;

  if (!conn)
    return DB_NO_CONNECTION;

  if (!conn->private) {
    /* added error element */
    return DB_NO_CONNECTION_REAL;
  }
  db_stmt_p stmt = malloc(sizeof(*stmt));
  sqlite3_stmt *sqlite_stmt = 0;
  /* TODO avoid strlen, use configurable limit? */
  rc = f.sqlite3_prepare_v2_fn(conn->private, sql, strlen(sql), &sqlite_stmt, (const char **) pptail);
  if (rc != SQLITE_OK)
    return db_sqlite_error_map(rc);
  stmt->private = sqlite_stmt;
  *stmt_p = stmt;
  stmt->get_column_text = db_sqlite_stmt_get_column_text;
  stmt->get_column_blob = db_sqlite_stmt_get_column_blob;
  stmt->get_column_int = db_sqlite_stmt_get_column_int;
  stmt->next = db_sqlite_stmt_next;
  stmt->close = db_sqlite_stmt_close;
  stmt->db = conn;
  return db_sqlite_error_map(rc);
}

const char *db_sqlite_stmt_get_column_text(db_stmt_p stmt, int column) {
  if (!stmt || !stmt->private) {
    return 0;
  }
  sqlite3_stmt *sqlite_stmt = stmt->private;
  const char *result  = f.sqlite3_column_text_fn(sqlite_stmt, column);
  /* error handling */
  return result; 
}

#if defined(HAVE_LIBSQLITE3)
const void* db_sqlite_stmt_get_column_blob(db_stmt_p stmtx, int col) {
       sqlite3_stmt *stmt = stmtx->private;
       return f.sqlite3_column_blob_fn(stmt,col);
}
#endif



int db_sqlite_stmt_get_column_int(db_stmt_p stmt, int column) {
  sqlite3_stmt *sqlite_stmt = stmt->private;
  int rc = f.sqlite3_column_int_fn(sqlite_stmt, column);
  return db_sqlite_error_map(rc);
}


int db_sqlite_stmt_next(db_stmt_p stmt)
{
  sqlite3_stmt *sqlite_stmt = stmt->private;
  int rc = f.sqlite3_step_fn(sqlite_stmt);
  return db_sqlite_error_map(rc);
}

int db_sqlite_stmt_close(db_stmt_p stmt)
{
  sqlite3_stmt *sqlite_stmt = stmt->private;
  int rc = f.sqlite3_finalize_fn(sqlite_stmt);
  free(stmt);
  return db_sqlite_error_map(rc);
}


int db_sqlite_upgrade_to_schema(int version)
{
	if (version < 0)
		return DB_OK;

	if (version > 0)
		return DB_ERROR;

	csync_debug(2, "Upgrading database schema to version %d.\n", version);

	csync_db_sql("Creating file table",
		"CREATE TABLE file ("
		"	filename, checktxt,"
		"	UNIQUE ( filename ) ON CONFLICT REPLACE"
		")");

	csync_db_sql("Creating dirty table",
		"CREATE TABLE dirty ("
		"	filename, forced, myname, peername,"
		"	UNIQUE ( filename, peername ) ON CONFLICT IGNORE"
		")");

	csync_db_sql("Creating hint table",
		"CREATE TABLE hint ("
		"	filename, recursive,"
		"	UNIQUE ( filename, recursive ) ON CONFLICT IGNORE"
		")");

	csync_db_sql("Creating action table",
		"CREATE TABLE action ("
		"	filename, command, logfile,"
		"	UNIQUE ( filename, command ) ON CONFLICT IGNORE"
		")");

	csync_db_sql("Creating x509_cert table",
		"CREATE TABLE x509_cert ("
		"	peername, certdata,"
		"	UNIQUE ( peername ) ON CONFLICT IGNORE"
		")");

	return DB_OK;
}

#endif
