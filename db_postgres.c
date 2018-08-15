/*
 *  Copyright (C) 2010  Dennis Schafroth <dennis@schafroth.com>
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
#include "db_postgres.h"
#include "dl.h"

#ifdef HAVE_POSTGRES
#include <libpq-fe.h>
#endif

#if (!defined HAVE_POSTGRES)
int db_postgres_open(const char *file, db_conn_p * conn_p)
{
	return DB_ERROR;
}
#else

static struct db_postgres_fns {
	PGconn *(*PQconnectdb_fn) (char *);
	ConnStatusType(*PQstatus_fn) (const PGconn *);
	char *(*PQerrorMessage_fn) (const PGconn *);
	void (*PQfinish_fn) (PGconn *);
	PGresult *(*PQexec_fn) (PGconn *, const char *);
	ExecStatusType(*PQresultStatus_fn) (const PGresult *);
	char *(*PQresultErrorMessage_fn) (const PGresult *);
	void (*PQclear_fn) (PGresult *);
	int (*PQntuples_fn) (const PGresult *);
	char *(*PQgetvalue_fn) (const PGresult *, int, int);
} f;

static void *dl_handle;

static void db_postgres_dlopen(void)
{
	csync_debug(2, "Opening shared library libpq.so.5\n");

	dl_handle = dlopen("libpq.so.5", RTLD_LAZY);
	if (dl_handle == NULL) {
		csync_fatal
		    ("Could not open libpq.so.5: %s\n"
		     "Please install postgres client library (libpg) or use other database (sqlite, mysql)\n",
		     dlerror());
	}
	csync_debug(2, "Reading symbols from shared library libpq.so.5\n");

	LOOKUP_SYMBOL(dl_handle, PQconnectdb);
	LOOKUP_SYMBOL(dl_handle, PQstatus);
	LOOKUP_SYMBOL(dl_handle, PQerrorMessage);
	LOOKUP_SYMBOL(dl_handle, PQfinish);
	LOOKUP_SYMBOL(dl_handle, PQexec);
	LOOKUP_SYMBOL(dl_handle, PQresultStatus);
	LOOKUP_SYMBOL(dl_handle, PQresultErrorMessage);
	LOOKUP_SYMBOL(dl_handle, PQclear);
	LOOKUP_SYMBOL(dl_handle, PQntuples);
	LOOKUP_SYMBOL(dl_handle, PQgetvalue);
}

/* Thi function parses a URL string like pgsql://[user[:passwd]@]hostname[:port]/database.
   and returns the result in the given parameters.

   If an optional keyword is not given, the value of the parameter is not changed.
*/

int db_postgres_open(const char *file, db_conn_p * conn_p)
{
	PGconn *pg_conn;
	char *host, *user, *pass, *database;
	unsigned int port = 5432;	/* default postgres port */
	char *db_url = strdup(file);
	char *pg_conn_info;

	db_postgres_dlopen();

	if (db_url == NULL)
		csync_fatal("No memory for db_url\n");

	csync_parse_url(db_url, &host, &user, &pass, &database, &port);
	ASPRINTF(&pg_conn_info, "host='%s' user='%s' password='%s' dbname='%s' port=%d",
		host ?: "localhost", user ?: "csync2", pass ?: "", database, port);

	pg_conn = f.PQconnectdb_fn(pg_conn_info);
	if (pg_conn == NULL)
		csync_fatal("No memory for postgress connection handle\n");

	if (f.PQstatus_fn(pg_conn) != CONNECTION_OK) {
		f.PQfinish_fn(pg_conn);
		free(pg_conn_info);

		ASPRINTF(&pg_conn_info, "host='%s' user='%s' password='%s' dbname='postgres' port=%d", host, user, pass, port);

		pg_conn = f.PQconnectdb_fn(pg_conn_info);
		if (pg_conn == NULL)
			csync_fatal("No memory for postgress connection handle\n");

		if (f.PQstatus_fn(pg_conn) != CONNECTION_OK) {
			csync_debug(0, "Connection failed: %s", f.PQerrorMessage_fn(pg_conn));
			f.PQfinish_fn(pg_conn);
			free(pg_conn_info);
			return DB_ERROR;
		} else {
			char *create_database_statement;
			PGresult *res;

			csync_debug(1, "Database %s not found, trying to create it ...", database);
			ASPRINTF(&create_database_statement, "create database %s", database);
			res = f.PQexec_fn(pg_conn, create_database_statement);

			free(create_database_statement);

			switch (f.PQresultStatus_fn(res)) {
			case PGRES_COMMAND_OK:
			case PGRES_TUPLES_OK:
				break;

			default:
				csync_debug(0, "Could not create database %s: %s", database, f.PQerrorMessage_fn(pg_conn));
				return DB_ERROR;
			}

			f.PQfinish_fn(pg_conn);
			free(pg_conn_info);

			ASPRINTF(&pg_conn_info, "host='%s' user='%s' password='%s' dbname='%s' port=%d",
				 host, user, pass, database, port);

			pg_conn = f.PQconnectdb_fn(pg_conn_info);
			if (pg_conn == NULL)
				csync_fatal("No memory for postgress connection handle\n");

			if (f.PQstatus_fn(pg_conn) != CONNECTION_OK) {
				csync_debug(0, "Connection failed: %s", f.PQerrorMessage_fn(pg_conn));
				f.PQfinish_fn(pg_conn);
				free(pg_conn_info);
				return DB_ERROR;
			}
		}
	}

	db_conn_p conn = calloc(1, sizeof(*conn));

	if (conn == NULL)
		csync_fatal("No memory for conn\n");

	*conn_p = conn;
	conn->private = pg_conn;
	conn->close = db_postgres_close;
	conn->exec = db_postgres_exec;
	conn->errmsg = db_postgres_errmsg;
	conn->prepare = db_postgres_prepare;
	conn->upgrade_to_schema = db_postgres_upgrade_to_schema;

	free(pg_conn_info);

	return DB_OK;
}

void db_postgres_close(db_conn_p conn)
{
	if (!conn)
		return;
	if (!conn->private)
		return;
	f.PQfinish_fn(conn->private);
	conn->private = 0;
}

const char *db_postgres_errmsg(db_conn_p conn)
{
	if (!conn)
		return "(no connection)";
	if (!conn->private)
		return "(no private data in conn)";
	return f.PQerrorMessage_fn(conn->private);
}

int db_postgres_exec(db_conn_p conn, const char *sql)
{
	PGresult *res;

	if (!conn)
		return DB_NO_CONNECTION;

	if (!conn->private) {
		/* added error element */
		return DB_NO_CONNECTION_REAL;
	}
	res = f.PQexec_fn(conn->private, sql);
	switch (f.PQresultStatus_fn(res)) {
	case PGRES_COMMAND_OK:
	case PGRES_TUPLES_OK:
		return DB_OK;

	default:
		return DB_ERROR;
	}
}

int db_postgres_prepare(db_conn_p conn, const char *sql, db_stmt_p * stmt_p, char **pptail)
{
	PGresult *result;
	int *row_p;

	*stmt_p = NULL;

	if (!conn)
		return DB_NO_CONNECTION;

	if (!conn->private) {
		/* added error element */
		return DB_NO_CONNECTION_REAL;
	}
	result = f.PQexec_fn(conn->private, sql);

	if (result == NULL)
		csync_fatal("No memory for result\n");

	switch (f.PQresultStatus_fn(result)) {
	case PGRES_COMMAND_OK:
	case PGRES_TUPLES_OK:
		break;

	default:
		csync_debug(1, "Error in PQexec: %s", f.PQresultErrorMessage_fn(result));
		f.PQclear_fn(result);
		return DB_ERROR;
	}

	row_p = malloc(sizeof(*row_p));
	if (row_p == NULL)
		csync_fatal("No memory for row\n");
	*row_p = -1;

	db_stmt_p stmt = malloc(sizeof(*stmt));
	if (stmt == NULL)
		csync_fatal("No memory for stmt\n");

	stmt->private = result;
	stmt->private2 = row_p;

	*stmt_p = stmt;
	stmt->get_column_text = db_postgres_stmt_get_column_text;
	stmt->get_column_blob = db_postgres_stmt_get_column_blob;
	stmt->get_column_int = db_postgres_stmt_get_column_int;
	stmt->next = db_postgres_stmt_next;
	stmt->close = db_postgres_stmt_close;
	stmt->db = conn;
	return DB_OK;
}

const void *db_postgres_stmt_get_column_blob(db_stmt_p stmt, int column)
{
	PGresult *result;
	int *row_p;

	if (!stmt || !stmt->private || !stmt->private2) {
		return 0;
	}
	result = (PGresult *) stmt->private;
	row_p = (int *)stmt->private2;

	if (*row_p >= f.PQntuples_fn(result) || *row_p < 0) {
		csync_debug(1, "row index out of range (should be between 0 and %d, is %d)\n", *row_p, f.PQntuples_fn(result));
		return NULL;
	}
	return f.PQgetvalue_fn(result, *row_p, column);
}

const char *db_postgres_stmt_get_column_text(db_stmt_p stmt, int column)
{
	PGresult *result;
	int *row_p;

	if (!stmt || !stmt->private || !stmt->private2) {
		return 0;
	}
	result = (PGresult *) stmt->private;
	row_p = (int *)stmt->private2;

	if (*row_p >= f.PQntuples_fn(result) || *row_p < 0) {
		csync_debug(1, "row index out of range (should be between 0 and %d, is %d)\n", *row_p, f.PQntuples_fn(result));
		return NULL;
	}
	return f.PQgetvalue_fn(result, *row_p, column);
}

int db_postgres_stmt_get_column_int(db_stmt_p stmt, int column)
{
	PGresult *result;
	int *row_p;

	if (!stmt || !stmt->private || !stmt->private2) {
		return 0;
	}
	result = (PGresult *) stmt->private;
	row_p = (int *)stmt->private2;

	if (*row_p >= f.PQntuples_fn(result) || *row_p < 0) {
		csync_debug(1, "row index out of range (should be between 0 and %d, is %d)\n", *row_p, f.PQntuples_fn(result));
		return 0;
	}
	return atoi(f.PQgetvalue_fn(result, *row_p, column));
}

int db_postgres_stmt_next(db_stmt_p stmt)
{
	PGresult *result;
	int *row_p;

	if (!stmt || !stmt->private || !stmt->private2) {
		return 0;
	}
	result = (PGresult *) stmt->private;
	row_p = (int *)stmt->private2;

	(*row_p)++;
	if (*row_p >= f.PQntuples_fn(result))
		return DB_DONE;

	return DB_ROW;
}

int db_postgres_stmt_close(db_stmt_p stmt)
{
	PGresult *res = stmt->private;

	f.PQclear_fn(res);
	free(stmt->private2);
	free(stmt);
	return DB_OK;
}

int db_postgres_upgrade_to_schema(int version)
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
		     "  UNIQUE (filename,command)"
		     ");");

	csync_db_sql("Creating dirty table",
		     "CREATE TABLE dirty ("
		     "  filename TEXT NOT NULL,"
		     "  forced INTEGER NOT NULL,"
		     "  myname TEXT NOT NULL,"
		     "  peername TEXT NOT NULL,"
		     "  UNIQUE (filename,peername)"
		     ");");

	csync_db_sql("Creating file table",
		     "CREATE TABLE file ("
		     "  filename TEXT NOT NULL,"
		     "  checktxt TEXT NOT NULL,"
		     "  UNIQUE (filename)"
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
		     "  UNIQUE (peername)"
		     ");");
	/* *INDENT-ON* */

	return DB_OK;
}

#endif /* HAVE_POSTGRES */
