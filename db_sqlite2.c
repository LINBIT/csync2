/*
 *  Copyright (C) 2010  Dennis Schafroth <dennis@schafroth.com>>
 *  Copyright (C) 2010  Johannes Thoma <johannes.thoma@gmx.at>
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

#include "db_api.h"
#include "config.h"

#ifndef HAVE_SQLITE
/* dummy function to implement a open that fails */
int db_sqlite2_open(const char *file, db_conn_p *conn_p) {
  return DB_ERROR;
}
#else 

#include <sqlite.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "db_sqlite2.h"
#include <dl.h>


static struct db_sqlite_fns {
	sqlite *(*sqlite_open_fn)(const char *, int, char**);
	void (*sqlite_close_fn)(sqlite *);
	int (*sqlite_exec_fn)(sqlite *, char *, int (*)(void*,int,char**,char**), void *, char **);
	int (*sqlite_compile_fn)(sqlite *, const char *, const char **, sqlite_vm **, char **);
	int (*sqlite_step_fn)(sqlite_vm *, int *, const char ***, const char ***);
	int (*sqlite_finalize_fn)(sqlite_vm *, char **);
} f;

static char *errmsg;

static void *dl_handle;


static void db_sqlite_dlopen(void)
{
	csync_debug(1, "Opening shared library libsqlite.so\n");

        dl_handle = dlopen("libsqlite.so", RTLD_LAZY);
        if (dl_handle == NULL) {
		csync_debug(1, "Libsqlite.so not found, trying libsqlite.so.0\n");
		dl_handle = dlopen("libsqlite.so.0", RTLD_LAZY);
	        if (dl_handle == NULL) {
			csync_fatal("Could not open libsqlite.so: %s\nPlease install sqlite client library (libsqlite) or use other database (postgres, mysql)\n", dlerror());
		}
        }
	csync_debug(1, "Opening shared library libsqlite.so\n");

        LOOKUP_SYMBOL(dl_handle, sqlite_open);
        LOOKUP_SYMBOL(dl_handle, sqlite_close);
        LOOKUP_SYMBOL(dl_handle, sqlite_exec);
        LOOKUP_SYMBOL(dl_handle, sqlite_compile);
        LOOKUP_SYMBOL(dl_handle, sqlite_step);
        LOOKUP_SYMBOL(dl_handle, sqlite_finalize);

}


int db_sqlite2_open(const char *file, db_conn_p *conn_p)
{
  db_sqlite_dlopen();

  sqlite *db = f.sqlite_open_fn(file, 0, &errmsg);
  if ( db == 0 ) {
    return DB_ERROR;
  };
  db_conn_p conn = calloc(1, sizeof(*conn));
  if (conn == NULL) {
    return DB_ERROR;
  }
  *conn_p = conn;
  conn->private = db;
  conn->close = db_sqlite2_close;
  conn->exec = db_sqlite2_exec;
  conn->prepare = db_sqlite2_prepare;
  conn->errmsg  = NULL;
  conn->upgrade_to_schema = db_sqlite2_upgrade_to_schema;
  return DB_OK;
}

void db_sqlite2_close(db_conn_p conn)
{
  if (!conn)
    return;
  if (!conn->private) 
    return;
  f.sqlite_close_fn(conn->private);
  conn->private = 0;
}

const char *db_sqlite2_errmsg(db_conn_p conn)
{
  if (!conn)
    return "(no connection)";
  if (!conn->private)
    return "(no private data in conn)";
  return errmsg;
}

int db_sqlite2_exec(db_conn_p conn, const char *sql) {
  int rc;
  if (!conn) 
    return DB_NO_CONNECTION; 

  if (!conn->private) {
    /* added error element */
    return DB_NO_CONNECTION_REAL;
  }
  rc = f.sqlite_exec_fn(conn->private, (char*) sql, 0, 0, &errmsg);
  /* On error parse, create DB ERROR element */
  return rc;
}

int db_sqlite2_prepare(db_conn_p conn, const char *sql, db_stmt_p *stmt_p, char **pptail) {
  int rc;
  sqlite *db;

  *stmt_p = NULL;

  if (!conn)
    return DB_NO_CONNECTION;

  if (!conn->private) {
    /* added error element */
    return DB_NO_CONNECTION_REAL;
  }
  db = conn->private;

  db_stmt_p stmt = malloc(sizeof(*stmt));
  sqlite_vm *sqlite_stmt = 0;
  rc = f.sqlite_compile_fn(db, sql, 0, &sqlite_stmt, &errmsg);
  if (rc != SQLITE_OK)
    return 0;
  stmt->private = sqlite_stmt;
  *stmt_p = stmt;
  stmt->get_column_text = db_sqlite2_stmt_get_column_text;
  stmt->get_column_blob = db_sqlite2_stmt_get_column_blob;
  stmt->get_column_int  = db_sqlite2_stmt_get_column_int;
  stmt->next  = db_sqlite2_stmt_next;
  stmt->close = db_sqlite2_stmt_close;
  stmt->db = conn;
  return DB_OK;
}

const char *db_sqlite2_stmt_get_column_text(db_stmt_p stmt, int column) {
  if (!stmt || !stmt->private) {
    return 0;
  }
  sqlite_vm *sqlite_stmt = stmt->private;
  const char **values = stmt->private2;
  return values[column];
}

const void* db_sqlite2_stmt_get_column_blob(db_stmt_p stmt, int col) {
       return db_sqlite2_stmt_get_column_text(stmt, col);
}

int db_sqlite2_stmt_get_column_int(db_stmt_p stmt, int column) {
  sqlite_vm *sqlite_stmt = stmt->private;
  const char **values = stmt->private2;
  const char *str_value = values[column];
  int value = 0;
  if (value) 
    value = atoi(str_value);
  /* TODO missing way to return error  */
  return value;
}


int db_sqlite2_stmt_next(db_stmt_p stmt)
{
  sqlite_vm *sqlite_stmt = stmt->private;
  const char **dataSQL_V, **dataSQL_N; 
  const char **values; 
  const char **names; 
  int columns;

  int rc = f.sqlite_step_fn(sqlite_stmt, &columns, &values, &names);
  stmt->private2 = values;
  /* TODO error mapping */ 
  return rc; //  == SQLITE_ROW;
}

int db_sqlite2_stmt_close(db_stmt_p stmt)
{
  sqlite_vm *sqlite_stmt = stmt->private;
  int rc = f.sqlite_finalize_fn(sqlite_stmt, &errmsg);
  free(stmt);
  return rc; 
}


int db_sqlite2_upgrade_to_schema(int version)
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
