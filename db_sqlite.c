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

#ifndef HAVE_LIBSQLITE3
int db_sqlite_open(const char *file, db_conn_p *conn_p) {
  return DB_FAIL;
}
#else

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
  int rc = sqlite3_open(file, &db);
  if ( rc != SQLITE_OK ) {
    return db_sqlite_error_map(rc);
  };
  db_conn_p conn = malloc(sizeof(*conn));
  *conn_p = conn;
  conn->private = db;
  conn->close   = db_sqlite_close;
  conn->exec    = db_sqlite_exec;
  conn->prepare = db_sqlite_prepare;
  conn->errmsg  = db_sqlite_errmsg;
  return db_sqlite_error_map(rc);
}

void db_sqlite_close(db_conn_p conn)
{
  if (!conn)
    return;
  if (!conn->private) 
    return;
  sqlite3_close(conn->private);
  conn->private = 0;
}

const char *db_sqlite_errmsg(db_conn_p conn)
{
  if (!conn)
    return "(no connection)";
  if (!conn->private)
    return "(no private data in conn)";
  return sqlite3_errmsg(conn->private);
}

int db_sqlite_exec(db_conn_p conn, const char *sql) {
  int rc;
  if (!conn) 
    return DB_NO_CONNECTION; 

  if (!conn->private) {
    /* added error element */
    return DB_NO_CONNECTION_REAL;
  }
  rc = sqlite3_exec(conn->private, sql, 0, 0, 0);
  return db_sqlite_error_map(rc);
}

int db_sqlite_prepare(db_conn_p conn, const char *sql, db_stmt_p *stmt_p, char **pptail) {
  int rc;
  if (!conn)
    return DB_NO_CONNECTION;

  if (!conn->private) {
    /* added error element */
    return DB_NO_CONNECTION_REAL;
  }
  db_stmt_p stmt = malloc(sizeof(*stmt));
  sqlite3_stmt *sqlite_stmt = 0;
  /* TODO avoid strlen, use configurable limit? */
  rc = sqlite3_prepare_v2(conn->private, sql, strlen(sql), &sqlite_stmt, (const char **) pptail);
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
  const char *result  = sqlite3_column_text(sqlite_stmt, column);
  /* error handling */
  return result; 
}

#if defined(HAVE_LIBSQLITE3)
const void* db_sqlite_stmt_get_column_blob(db_stmt_p stmtx, int col) {
       sqlite3_stmt *stmt = stmtx->private;
       return sqlite3_column_blob(stmt,col);
}
#endif



int db_sqlite_stmt_get_column_int(db_stmt_p stmt, int column) {
  sqlite3_stmt *sqlite_stmt = stmt->private;
  int rc = sqlite3_column_int(sqlite_stmt, column);
  return db_sqlite_error_map(rc);
}


int db_sqlite_stmt_next(db_stmt_p stmt)
{
  sqlite3_stmt *sqlite_stmt = stmt->private;
  int rc = sqlite3_step(sqlite_stmt);
  return db_sqlite_error_map(rc);
}

int db_sqlite_stmt_close(db_stmt_p stmt)
{
  sqlite3_stmt *sqlite_stmt = stmt->private;
  int rc = sqlite3_finalize(sqlite_stmt);
  free(stmt);
  return db_sqlite_error_map(rc);
}
#endif
