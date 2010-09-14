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
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include "db_api.h"
#include "db_mysql.h"

#ifdef HAVE_LIBMYSQLCLIENT
#include <mysql/mysql.h>
#endif


static int mysql_is_initialized = 0;

int db_mysql_parse_url(char *url, char **host, char **user, char **pass, char **database, unsigned int *port, char **unix_socket) 
{
  char *pos = strchr(url, '@'); 
  if (pos) {
    // Optional user/passwd
    *(pos) = 0;
    *(user) = url;
    url = pos + 1;
    // TODO password
    pos = strchr(*user, ':');
    if (pos) {
      *(pos) = 0;
      *(pass) = (pos +1);
    }
    else
      *pass = 0;
  }
  else {
    // No user/pass password 
    *user = 0;
    *pass = 0;
  }
  *host = url;
  pos = strchr(*host, '/');
  if (pos) {
    // Database
    (*pos) = 0;
    *database = pos+1;
  }
  else {
    *database = 0;
  }
  pos = strchr(*host, ':');
  if (pos) {
    (*pos) = 0;
    *port = atoi(pos+1);
  }
  *unix_socket = 0;
  return DB_OK;
}

int db_mysql_open(const char *file, db_conn_p *conn_p)
{
#ifdef HAVE_LIBMYSQLCLIENT
  MYSQL *db = mysql_init(0);
  char *host, *user, *pass, *database, *unix_socket;
  unsigned int port;
  char *db_url = malloc(strlen(file)+1);
  strcpy(db_url, file);
  int rc =db_mysql_parse_url(db_url, &host, &user, &pass, &database, &port, &unix_socket);
  if (rc != DB_OK) {
    return rc;
  }

  if (mysql_real_connect(db, host, user, pass, database, port, unix_socket, 0) == NULL) {
    csync_fatal("Failed to connect to database: Error: %s\n", mysql_error(db));
  }

  db_conn_p conn = calloc(1, sizeof(*conn));
  if (conn == NULL) {
    return DB_ERROR;
  }
  *conn_p = conn;
  conn->private = db;
  conn->close = db_mysql_close;
  conn->exec = db_mysql_exec;
  conn->prepare = db_mysql_prepare;
  conn->errmsg = db_mysql_errmsg;
  conn->upgrade_to_schema = db_mysql_upgrade_to_schema;

  return rc;
#else
  return DB_ERROR;
#endif
}

#ifdef HAVE_LIBMYSQLCLIENT

void db_mysql_close(db_conn_p conn)
{
  if (!conn)
    return;
  if (!conn->private) 
    return;
  mysql_close(conn->private);
  conn->private = 0;
}

const char *db_mysql_errmsg(db_conn_p conn)
{
  if (!conn)
    return "(no connection)";
  if (!conn->private)
    return "(no private data in conn)";
  return mysql_error(conn->private);
}

int db_mysql_exec(db_conn_p conn, const char *sql) {
  int rc = DB_ERROR;
  if (!conn) 
    return DB_NO_CONNECTION; 

  if (!conn->private) {
    /* added error element */
    return DB_NO_CONNECTION_REAL;
  }
  rc = mysql_query(conn->private, sql);
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
  MYSQL_RES *mysql_stmt = mysql_store_result(conn->private);
  if (mysql_stmt == NULL) {
    csync_debug(4, "Error in mysql_store_result: %s", mysql_error(conn->private));
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
		"  `filename` varchar(255) CHARACTER SET utf8 COLLATE utf8_bin DEFAULT NULL,"
		"  `command` text,"
		"  `logfile` text,"
		"  UNIQUE KEY `filename` (`filename`,`command`(20))"
		")"
		) != DB_OK)
		return DB_ERROR;

	if (db_exec(db,
		"CREATE TABLE `dirty` ("
		"  `filename` varchar(255) CHARACTER SET utf8 COLLATE utf8_bin DEFAULT NULL,"
		"  `forced`   int(11)      DEFAULT NULL,"
		"  `myname`   varchar(50)  DEFAULT NULL,"
		"  `peername` varchar(50)  DEFAULT NULL,"
		"  UNIQUE KEY `filename` (`filename`,`peername`),"
		"  KEY `dirty_host` (`peername`(10))"
		")"
		) != DB_OK)
		return DB_ERROR;

	if (db_exec(db,
		"CREATE TABLE `file` ("
		"  `filename` varchar(255) CHARACTER SET utf8 COLLATE utf8_bin DEFAULT NULL,"
		"  `checktxt` varchar(200) DEFAULT NULL,"
		"  UNIQUE KEY `filename` (`filename`)"
		")"
		) != DB_OK)
		return DB_ERROR;

	if (db_exec(db,
		"CREATE TABLE `hint` ("
		"  `filename` varchar(255) CHARACTER SET utf8 COLLATE utf8_bin DEFAULT NULL,"
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


#endif
