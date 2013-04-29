/*
   DB API
   
 */

#include "csync2.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "db_api.h"

#include "db_mysql.h"
#include "db_postgres.h"
#include "db_sqlite.h"
#include "db_sqlite2.h"

#define DEADLOCK_MESSAGE \
	"Database backend is exceedingly busy => Terminating (requesting retry).\n"

int db_detect_type(const char **db_str, int type)
{
	const char *db_types[] = { "mysql://", "sqlite3://", "sqlite2://", "pgsql://", 0 };
	int types[] = { DB_MYSQL, DB_SQLITE3, DB_SQLITE2, DB_PGSQL };
	int index;
	for (index = 0; 1; index++) {
		if (db_types[index] == 0)
			break;
		if (!strncmp(*db_str, db_types[index], strlen(db_types[index]))) {
			*db_str += strlen(db_types[index]);
			return types[index];
		}
	}
	return type;
}

/* expects the "scheme://" prefix to be removed already!
 * eg. if full url had been
 *   mysql://user:password@host:port/db
 * input url to this function:
 *   user:password@host:port/db
 * Note:
 *   if no database is specified, it will default to
 *      csync2_<thishostname>_<configname>
 *   or csync2_<thishostname>, if no explicit config name is used.
 *
 * Note that the output arguments *host, *user, *pass and *database will be initialized.
 * *port should be initialized to the default port before calling this function.
 *
 * TODO:
 * add support for unix domain sockets?
 **/
void csync_parse_url(char *url, char **host, char **user, char **pass, char **database, unsigned int *port)
{
	char *pos = strchr(url, '@');
	if (pos) {
		/* Optional user/passwd */
		*pos = '\0';
		*user = url;
		url = pos + 1;
		/* password */
		pos = strchr(*user, ':');
		if (pos) {
			*pos = '\0';
			*pass = pos + 1;
		} else
			*pass = NULL;
	} else {
		/* No user:pass@ */
		*user = NULL;
		*pass = NULL;
	}
	*host = url;
	pos = strchr(*host, '/');
	if (pos) {
		// Database
		*pos = '\0';
		*database = pos + 1;
	} else
		*database = NULL;
	pos = strchr(*host, ':');
	if (pos) {
		*pos = '\0';
		*port = atoi(pos + 1);
	}

	if (!*database || !*database[0])
		ASPRINTF(database, "csync2_%s%s%s", myhostname, cfgname[0] ? "_" : "", cfgname);

	/* I just don't want to know about special characters,
	 * or differences between db engines. */
	for (pos = *database; *pos; pos++) {
		switch (*pos) {
		case 'a' ... 'z':
		case 'A' ... 'Z':
		case '0' ... '9':
		case '_':
			break;
		default:
			*pos = '_';
		}
	}
}

int db_open(const char *file, int type, db_conn_p * db)
{
	int rc = DB_ERROR;
	const char *db_str;
	db_str = file;

	type = db_detect_type(&db_str, type);
	/* Switch between implementation */
	switch (type) {
	case DB_SQLITE2:
		rc = db_sqlite2_open(db_str, db);

		if (rc != DB_OK && db_str[0] != '/')
			fprintf(csync_debug_out,
				"Cannot open database file: %s, maybe you need three slashes (like sqlite:///var/lib/csync2/csync2.db)\n",
				db_str);
		break;
	case DB_SQLITE3:
		rc = db_sqlite_open(db_str, db);

		if (rc != DB_OK && db_str[0] != '/')
			fprintf(csync_debug_out,
				"Cannot open database file: %s, maybe you need three slashes (like sqlite:///var/lib/csync2/csync2.db)\n",
				db_str);
		break;
#ifdef HAVE_MYSQL
	case DB_MYSQL:
		rc = db_mysql_open(db_str, db);
		break;
#else
	case DB_MYSQL:
		csync_fatal("No Mysql support configured. Please reconfigure with --enable-mysql (database is %s).\n", file);
		rc = DB_ERROR;
		break;
#endif
#ifdef HAVE_POSTGRES
	case DB_PGSQL:
		rc = db_postgres_open(db_str, db);
		break;
#else
	case DB_PGSQL:
		csync_fatal("No Postgres SQL support configured. Please reconfigure with --enable-postgres (database is %s).\n",
			    file);
		rc = DB_ERROR;
		break;
#endif

	default:
		csync_fatal("Database type not found. Can't open database %s\n", file);
		rc = DB_ERROR;
	}
	if (*db)
		(*db)->logger = 0;
	return rc;
}

void db_set_logger(db_conn_p conn, void (*logger) (int lv, const char *fmt, ...))
{
	if (conn == NULL)
		csync_fatal("No connection in set_logger.\n");

	conn->logger = logger;
}

void db_close(db_conn_p conn)
{
	if (!conn || !conn->close)
		return;
	conn->close(conn);
}

const char *db_errmsg(db_conn_p conn)
{
	if (conn && conn->errmsg)
		return conn->errmsg(conn);

	return "(no error message function available)";
}

int db_exec(db_conn_p conn, const char *sql)
{
	if (conn && conn->exec)
		return conn->exec(conn, sql);

	csync_debug(0, "No exec function in db_exec.\n");
	return DB_ERROR;
}

int db_prepare_stmt(db_conn_p conn, const char *sql, db_stmt_p * stmt, char **pptail)
{
	if (conn && conn->prepare)
		return conn->prepare(conn, sql, stmt, pptail);

	csync_debug(0, "No prepare function in db_prepare_stmt.\n");
	return DB_ERROR;
}

const char *db_stmt_get_column_text(db_stmt_p stmt, int column)
{
	if (stmt && stmt->get_column_text)
		return stmt->get_column_text(stmt, column);

	csync_debug(0, "No stmt in db_stmt_get_column_text / no function.\n");
	return NULL;
}

int db_stmt_get_column_int(db_stmt_p stmt, int column)
{
	if (stmt && stmt->get_column_int)
		return stmt->get_column_int(stmt, column);

	csync_debug(0, "No stmt in db_stmt_get_column_int / no function.\n");
	return 0;
}

int db_stmt_next(db_stmt_p stmt)
{
	if (stmt && stmt->next)
		return stmt->next(stmt);

	csync_debug(0, "No stmt in db_stmt_next / no function.\n");
	return DB_ERROR;
}

int db_stmt_close(db_stmt_p stmt)
{
	if (stmt && stmt->close)
		return stmt->close(stmt);

	csync_debug(0, "No stmt in db_stmt_close / no function.\n");
	return DB_ERROR;
}

int db_schema_version(db_conn_p db)
{
	int version = -1;

	SQL_BEGIN(NULL,		/* ignore errors */
		  "SELECT count(*) from file") {
		version = 0;
	} SQL_END;

	return version;
}

int db_upgrade_to_schema(db_conn_p db, int version)
{
	if (db && db->upgrade_to_schema)
		return db->upgrade_to_schema(version);

	return DB_ERROR;
}
