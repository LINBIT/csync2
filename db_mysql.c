/*
 *  Copyright (C) 2010  Dennis Schafroth <dennis@schafroth.com>>
 *  Copyright (C) 2010  Johannes Thoma <johannes.thoma@gmx.at>
 *  Copyright (C) 2010 - 2013 LINBIT Information Technologies GmbH
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
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include "db_api.h"
#include "db_mysql.h"
#include "dl.h"

#ifdef HAVE_MYSQL
#include <mysql/mysql.h>
#include <mysql/mysqld_error.h>

static struct db_mysql_fns {
	MYSQL *(*mysql_init_fn) (MYSQL *);
	MYSQL *(*mysql_real_connect_fn) (MYSQL *, const char *, const char *, const char *, const char *,
					 unsigned int, const char *, unsigned long);
	int (*mysql_errno_fn) (MYSQL *);
	int (*mysql_query_fn) (MYSQL *, const char *);
	void (*mysql_close_fn) (MYSQL *);
	const char *(*mysql_error_fn) (MYSQL *);
	MYSQL_RES *(*mysql_store_result_fn) (MYSQL *);
	unsigned int (*mysql_num_fields_fn) (MYSQL_RES *);
	MYSQL_ROW(*mysql_fetch_row_fn) (MYSQL_RES *);
	void (*mysql_free_result_fn) (MYSQL_RES *);
	unsigned int (*mysql_warning_count_fn) (MYSQL *);
} f;

static void *dl_handle;

static void db_mysql_dlopen(void)
{
	csync_debug(2, "Opening shared library libmysqlclient.so.18\n");
	dl_handle = dlopen("libmysqlclient.so.18", RTLD_LAZY);
	if (dl_handle == NULL) {
		csync_fatal
		    ("Could not open libmysqlclient.so.18: %s\n"
		     "Please install Mysql client library (libmysqlclient) or use other database (sqlite, postgres)\n",
		     dlerror());
	}

	csync_debug(2, "Reading symbols from shared library libmysqlclient.so.18\n");

	LOOKUP_SYMBOL(dl_handle, mysql_init);
	LOOKUP_SYMBOL(dl_handle, mysql_real_connect);
	LOOKUP_SYMBOL(dl_handle, mysql_errno);
	LOOKUP_SYMBOL(dl_handle, mysql_query);
	LOOKUP_SYMBOL(dl_handle, mysql_close);
	LOOKUP_SYMBOL(dl_handle, mysql_error);
	LOOKUP_SYMBOL(dl_handle, mysql_store_result);
	LOOKUP_SYMBOL(dl_handle, mysql_num_fields);
	LOOKUP_SYMBOL(dl_handle, mysql_fetch_row);
	LOOKUP_SYMBOL(dl_handle, mysql_free_result);
	LOOKUP_SYMBOL(dl_handle, mysql_warning_count);
}

int db_mysql_open(const char *file, db_conn_p * conn_p)
{
	db_mysql_dlopen();

	MYSQL *db = f.mysql_init_fn(0);
	char *host, *user, *pass, *database;
	unsigned int port = 0;
	char *db_url = malloc(strlen(file) + 1);
	char *create_database_statement;

	if (db_url == NULL)
		csync_fatal("No memory for db_url\n");

	strcpy(db_url, file);
	csync_parse_url(db_url, &host, &user, &pass, &database, &port);

	if (f.mysql_real_connect_fn(db, host, user, pass, database, port, NULL, 0) == NULL) {
		if (f.mysql_errno_fn(db) != ER_BAD_DB_ERROR)
			goto fatal;
		if (f.mysql_real_connect_fn(db, host, user, pass, NULL, port, NULL, 0) == NULL)
			goto fatal;

		ASPRINTF(&create_database_statement, "create database %s", database);

		csync_debug(2, "creating database %s\n", database);
		if (f.mysql_query_fn(db, create_database_statement) != 0)
			csync_fatal("Cannot create database %s: Error: %s\n", database, f.mysql_error_fn(db));
		free(create_database_statement);

		f.mysql_close_fn(db);
		db = f.mysql_init_fn(0);

		if (f.mysql_real_connect_fn(db, host, user, pass, database, port, NULL, 0) == NULL)
			goto fatal;
	}

	db_conn_p conn = calloc(1, sizeof(*conn));
	if (conn == NULL)
		return DB_ERROR;
	*conn_p = conn;
	conn->private = db;
	conn->close = db_mysql_close;
	conn->exec = db_mysql_exec;
	conn->prepare = db_mysql_prepare;
	conn->errmsg = db_mysql_errmsg;
	conn->upgrade_to_schema = db_mysql_upgrade_to_schema;

	return DB_OK;
fatal:
	csync_fatal("Failed to connect to database: Error: %s\n", f.mysql_error_fn(db));
	return DB_ERROR;
}

void db_mysql_close(db_conn_p conn)
{
	if (!conn)
		return;
	if (!conn->private)
		return;
	f.mysql_close_fn(conn->private);
	conn->private = 0;
}

const char *db_mysql_errmsg(db_conn_p conn)
{
	if (!conn)
		return "(no connection)";
	if (!conn->private)
		return "(no private data in conn)";
	return f.mysql_error_fn(conn->private);
}

static void print_warnings(int level, MYSQL * m)
{
	int rc;
	MYSQL_RES *res;
	int fields;
	MYSQL_ROW row;

	if (m == NULL)
		csync_fatal("print_warnings: m is NULL");

	rc = f.mysql_query_fn(m, "SHOW WARNINGS");
	if (rc != 0)
		csync_fatal("print_warnings: Failed to get warning messages");

	res = f.mysql_store_result_fn(m);
	if (res == NULL)
		csync_fatal("print_warnings: Failed to get result set for warning messages");

	fields = f.mysql_num_fields_fn(res);
	if (fields < 2)
		csync_fatal("print_warnings: Strange: show warnings result set has less than 2 rows");

	row = f.mysql_fetch_row_fn(res);

	while (row) {
		csync_debug(level, "MySql Warning: %s\n", row[2]);
		row = f.mysql_fetch_row_fn(res);
	}

	f.mysql_free_result_fn(res);
}

int db_mysql_exec(db_conn_p conn, const char *sql)
{
	int rc = DB_ERROR;
	if (!conn)
		return DB_NO_CONNECTION;

	if (!conn->private) {
		/* added error element */
		return DB_NO_CONNECTION_REAL;
	}
	rc = f.mysql_query_fn(conn->private, sql);

	/* Treat warnings as errors.
	 * For example when a column is too short this should be an error. */
	if (f.mysql_warning_count_fn(conn->private) > 0) {
		print_warnings(1, conn->private);
		return DB_ERROR;
	}

	/* On error parse, create DB ERROR element */
	return rc;
}

int db_mysql_prepare(db_conn_p conn, const char *sql, db_stmt_p * stmt_p, char **pptail)
{
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
	rc = f.mysql_query_fn(conn->private, sql);

	if (f.mysql_warning_count_fn(conn->private) > 0) {
		print_warnings(1, conn->private);
		return DB_ERROR;
	}

	MYSQL_RES *mysql_stmt = f.mysql_store_result_fn(conn->private);
	if (mysql_stmt == NULL) {
		csync_debug(2, "Error in mysql_store_result: %s\n", f.mysql_error_fn(conn->private));
		return DB_ERROR;
	}

	if (f.mysql_warning_count_fn(conn->private) > 0) {
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

const void *db_mysql_stmt_get_column_blob(db_stmt_p stmt, int column)
{
	if (!stmt || !stmt->private2) {
		return 0;
	}
	MYSQL_ROW row = stmt->private2;
	return row[column];
}

const char *db_mysql_stmt_get_column_text(db_stmt_p stmt, int column)
{
	if (!stmt || !stmt->private2) {
		return 0;
	}
	MYSQL_ROW row = stmt->private2;
	return row[column];
}

int db_mysql_stmt_get_column_int(db_stmt_p stmt, int column)
{
	const char *value = db_mysql_stmt_get_column_text(stmt, column);
	if (value)
		return atoi(value);
	/* error mapping */
	return 0;
}

int db_mysql_stmt_next(db_stmt_p stmt)
{
	MYSQL_RES *mysql_stmt = stmt->private;
	stmt->private2 = f.mysql_fetch_row_fn(mysql_stmt);
	/* error mapping */
	if (stmt->private2)
		return DB_ROW;
	return DB_DONE;
}

int db_mysql_stmt_close(db_stmt_p stmt)
{
	MYSQL_RES *mysql_stmt = stmt->private;
	f.mysql_free_result_fn(mysql_stmt);
	free(stmt);
	return DB_OK;
}

/* NOTE:
 * NI_MAXHOST is 1025.  That should be plenty for typical hostnames.
 *
 * Use TEXT fields:
 * PATH_MAX is typically 4096, and that is assumed elsewhere in the code as
 * well.  But as all filenames (and other interesting fields) are stored as
 * transformed by url_encode(), they can be three times as long.
 * checktxt may be very long, if it stores the target of a very long symlink.
 *
 * The INTEGER are actually used as boolean flags.
 *
 * prefix limit MyISAM: 1000 bytes, InnoDB: 767 bytes.
 * Which is also the max key length.
 * utf8: up to 3 bytes per "character" --> sum of key prefix lengths: 767/3 = 255
 *
 * We must not define UNIQUE keys on prefixes,
 * or we would not be able to handle long path names.
 * We should be able to get away with just "key",
 * typically the code does "delete from" before "insert into" anyways.
 * */
int db_mysql_upgrade_to_schema(int version)
{
	if (version < 0)
		return DB_OK;

	if (version > 0)
		return DB_ERROR;

	csync_debug(2, "Upgrading database schema to version %d.\n", version);

	/* *INDENT-OFF* */
	csync_db_sql("Creating action table",
		     "CREATE TABLE action ("
		     "  filename TEXT NOT NULL,"
		     "  command TEXT NOT NULL,"
		     "  logfile TEXT NOT NULL,"
		     "  KEY filename (filename(255)),"
		     "  KEY command (command(255))"
		     ");");

	csync_db_sql("Creating dirty table",
		     "CREATE TABLE dirty ("
		     "  filename TEXT NOT NULL,"
		     "  forced INTEGER NOT NULL,"
		     "  myname TEXT NOT NULL,"
		     "  peername TEXT NOT NULL,"
		     "  KEY filename (filename(255)),"
		     "  KEY dirty_host (peername(255))"
		     ");");

	csync_db_sql("Creating file table",
		     "CREATE TABLE file ("
		     "  filename TEXT NOT NULL,"
		     "  checktxt TEXT NOT NULL,"
		     "  KEY filename (filename(255))"
		     ");");

	csync_db_sql("Creating hint table",
		     "CREATE TABLE hint ("
		     "  filename TEXT NOT NULL,"
		     "  recursive INTEGER NOT NULL"
		     ");");

	csync_db_sql("Creating x509_cert table",
		     "CREATE TABLE x509_cert ("
		     "  peername TEXT NOT NULL,"
		     "  certdata TEXT NOT NULL,"
		     "  KEY peername (peername(255))"
		     ");");
	/* *INDENT-ON* */

	return DB_OK;
}

#endif
