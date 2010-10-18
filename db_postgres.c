/*
 *  
 *  
 *  Copyright (C) 2010  Dennis Schafroth <dennis@schafroth.com>
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

#include "csync2.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include "db_api.h"
#include "db_postgres.h"

#ifdef HAVE_POSTGRESQL_LIBPQ_FE_H
#include <postgresql/libpq-fe.h>
#endif

#if (!defined HAVE_POSTGRES)
int db_pgsql_open(const char *file, db_conn_p *conn_p)
{
	return DB_ERROR;
}
#else

/* This function parses a URL string like pgsql://[user[:passwd]@]hostname[:port]/database.
   and returns the result in the given parameters.

   If an optional keyword is not given, the value of the parameter is not changed.
*/

static int db_pgsql_parse_url(char *url, char **host, char **user, char **pass, char **database, unsigned int *port) 
{
  char *pos = strchr(url, '@'); 
  if (pos) {
    *(pos) = 0;
    *(user) = url;
    url = pos + 1;

    pos = strchr(*user, ':');
    if (pos) {
      *(pos) = 0;
      *(pass) = (pos +1);
    }
  }
  *host = url;
  pos = strchr(*host, '/');
  if (pos) {
    // Database
    (*pos) = 0;
    *database = pos+1;
  }
  pos = strchr(*host, ':');
  if (pos) {
    (*pos) = 0;
    *port = atoi(pos+1);
  }
  return DB_OK;
}

int db_pgsql_open(const char *file, db_conn_p *conn_p)
{
  PGconn *pg_conn;
  char *host, *user, *pass, *database;
  unsigned int port = 5432;  /* default postgres port */
  char *db_url = malloc(strlen(file)+1);
  char *create_database_statement;
  char *pg_conn_info;

  if (db_url == NULL)
    csync_fatal("No memory for db_url\n");

  user = "postgres";
  pass = "";
  host = "localhost";
  database = "csync2";

  strcpy(db_url, file);
  int rc = db_pgsql_parse_url(db_url, &host, &user, &pass, &database, &port);
  if (rc != DB_OK)
    return rc;

  ASPRINTF(&pg_conn_info, "host='%s' user='%s' password='%s' dbname='%s' port=%d",
	host, user, pass, database, port);

  pg_conn = PQconnectdb(pg_conn_info);
  if (pg_conn == NULL)
    csync_fatal("No memory for postgress connection handle\n");

  if (PQstatus(pg_conn) != CONNECTION_OK) {
    csync_debug(0, "Connection failed: %s", PQerrorMessage(pg_conn));
    PQfinish(pg_conn);
    return DB_ERROR;
  }

#if 0
    if (mysql_errno(db) == ER_BAD_DB_ERROR) {
      if (mysql_real_connect(db, host, user, pass, NULL, port, unix_socket, 0) != NULL) {
	ASPRINTF(&create_database_statement, "create database %s", database)

	csync_debug(2, "creating database %s\n", database);
        if (mysql_query(db, create_database_statement) != 0)
          csync_fatal("Cannot create database %s: Error: %s\n", database, mysql_error(db));
	free(create_database_statement);

	mysql_close(db);
	db = mysql_init(0);

        if (mysql_real_connect(db, host, user, pass, database, port, unix_socket, 0) == NULL)
          goto fatal;
      }
    } else
fatal:
      csync_fatal("Failed to connect to database: Error: %s\n", mysql_error(db));
  }
#endif

  db_conn_p conn = calloc(1, sizeof(*conn));

  if (conn == NULL)
    csync_fatal("No memory for conn\n");

  *conn_p = conn;
  conn->private = pg_conn;
  conn->close = db_postgres_close;
  conn->exec = db_postgres_exec;
  conn->prepare = db_postgres_prepare;
//  conn->errmsg = db_mysql_errmsg;
//  conn->upgrade_to_schema = db_mysql_upgrade_to_schema;

  return DB_OK;
}


void db_postgres_close(db_conn_p conn)
{
  if (!conn)
    return;
  if (!conn->private) 
    return;
  PGfinish(conn->private);
  conn->private = 0;
}

const char *db_postgres_errmsg(db_conn_p conn)
{
  if (!conn)
    return "(no connection)";
  if (!conn->private)
    return "(no private data in conn)";
  return PQerrorMessage(conn->private);
}


int db_postgres_exec(db_conn_p conn, const char *sql) 
{
  int rc = DB_ERROR;
  if (!conn)
    return DB_NO_CONNECTION;

  if (!conn->private) {
    /* added error element */
    return DB_NO_CONNECTION_REAL;
  }
  rc = mysql_query(conn->private, sql);

/* Treat warnings as errors. For example when a column is too short this should
   be an error. */

  if (mysql_warning_count(conn->private) > 0) {
    print_warnings(1, conn->private);
    return DB_ERROR;
  }

  /* On error parse, create DB ERROR element */
  return rc;
}

int db_mysql_prepare(db_conn_p conn, const char *sql, db_stmt_p *stmt_p, 
		     char **pptail) {
  int rc = DB_ERROR;

  *stmt_p = NULL;

  if (!conn)
    return DB_NO_CONNECTION;

  if (!conn->private) {
    /* added error element */
    return DB_NO_CONNECTION_REAL;
  }
  db_stmt_p stmt = malloc(sizeof(*stmt));
  /* TODO avoid strlen, use configurable limit? */
  rc = mysql_query(conn->private, sql);

/* Treat warnings as errors. For example when a column is too short this should
   be an error. */

  if (mysql_warning_count(conn->private) > 0) {
    print_warnings(1, conn->private);
    return DB_ERROR;
  }

  MYSQL_RES *mysql_stmt = mysql_store_result(conn->private);
  if (mysql_stmt == NULL) {
    csync_debug(2, "Error in mysql_store_result: %s", mysql_error(conn->private));
    return DB_ERROR;
  }

/* Treat warnings as errors. For example when a column is too short this should
   be an error. */

  if (mysql_warning_count(conn->private) > 0) {
    print_warnings(1, conn->private);
    return DB_ERROR;
  }

  stmt->private = mysql_stmt;
  /* TODO error mapping / handling */
  *stmt_p = stmt;
  stmt->get_column_text = db_mysql_stmt_get_column_text;
  stmt->get_column_blob = db_mysql_stmt_get_column_blob;
  stmt->get_column_int = db_mysql_stmt_get_column_int;
  stmt->next = db_mysql_stmt_next;
  stmt->close = db_mysql_stmt_close;
  stmt->db = conn;
  return DB_OK;
}

const void* db_mysql_stmt_get_column_blob(db_stmt_p stmt, int column) {
  if (!stmt || !stmt->private2) {
    return 0;
  }
  MYSQL_ROW row = stmt->private2;
  return row[column];
}

const char *db_mysql_stmt_get_column_text(db_stmt_p stmt, int column) {
  if (!stmt || !stmt->private2) {
    return 0;
  }
  MYSQL_ROW row = stmt->private2;
  return row[column];
}

int db_mysql_stmt_get_column_int(db_stmt_p stmt, int column) {
  const char *value = db_mysql_stmt_get_column_text(stmt, column);
  if (value)
    return atoi(value);
  /* error mapping */
  return 0;
}


int db_mysql_stmt_next(db_stmt_p stmt)
{
  MYSQL_RES *mysql_stmt = stmt->private;
  stmt->private2 = mysql_fetch_row(mysql_stmt);
  /* error mapping */ 
  if (stmt->private2)
    return DB_ROW;
  return DB_DONE;
}

int db_mysql_stmt_close(db_stmt_p stmt)
{
  MYSQL_RES *mysql_stmt = stmt->private;
  mysql_free_result(mysql_stmt);
  free(stmt);
  return DB_OK; 
}


int db_mysql_upgrade_to_schema(db_conn_p db, int version)
{
	if (version < 0)
		return DB_OK;

	if (version > 0)
		return DB_ERROR;

	csync_debug(2, "Upgrading database schema to version %d.\n", version);

	if (db_exec(db,
		"CREATE TABLE `action` ("
		"  `filename` varchar(4096) CHARACTER SET utf8 COLLATE utf8_bin DEFAULT NULL,"
		"  `command` text,"
		"  `logfile` text,"
		"  UNIQUE KEY `filename` (`filename`(326),`command`(20))"
		")"
		) != DB_OK)
		return DB_ERROR;

	if (db_exec(db,
		"CREATE TABLE `dirty` ("
		"  `filename` varchar(4096) CHARACTER SET utf8 COLLATE utf8_bin DEFAULT NULL,"
		"  `forced`   int(11)      DEFAULT NULL,"
		"  `myname`   varchar(50)  DEFAULT NULL,"
		"  `peername` varchar(50)  DEFAULT NULL,"
		"  UNIQUE KEY `filename` (`filename`(316),`peername`),"
		"  KEY `dirty_host` (`peername`(10))"
		")"
		) != DB_OK)
		return DB_ERROR;

	if (db_exec(db,
		"CREATE TABLE `file` ("
		"  `filename` varchar(4096) CHARACTER SET utf8 COLLATE utf8_bin DEFAULT NULL,"
		"  `checktxt` varchar(200) DEFAULT NULL,"
		"  UNIQUE KEY `filename` (`filename`(333))"
		")"
		) != DB_OK)
		return DB_ERROR;

	if (db_exec(db,
		"CREATE TABLE `hint` ("
		"  `filename` varchar(4096) CHARACTER SET utf8 COLLATE utf8_bin DEFAULT NULL,"
		"  `recursive` int(11)     DEFAULT NULL"
		")"
		) != DB_OK)
		return DB_ERROR;

	if (db_exec(db,
		"CREATE TABLE `x509_cert` ("
		"  `peername` varchar(50)  DEFAULT NULL,"
		"  `certdata` varchar(255) DEFAULT NULL,"
		"  UNIQUE KEY `peername` (`peername`)"
		")"
		) != DB_OK)
		return DB_ERROR;

	return DB_OK;
}


#endif /*0*/
#endif
