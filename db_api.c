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

#define DEADLOCK_MESSAGE \
	"Database backend is exceedingly busy => Terminating (requesting retry).\n"

int db_sqlite_open(const char *file, db_conn_p *db); 
int db_mysql_open(const char *file, db_conn_p *db); 

int db_detect_type(const char **db_str, int type) {
  const char *db_types[] = { "mysql://", "sqlite3://", "sqlite2://", "pgsql://", 0 };
  int types[]            = { DB_MYSQL,   DB_SQLITE2,   DB_SQLITE3,   DB_PGSQL }; 
  int index; 
  for (index = 0; 1 ; index++) {
    if (db_types[index] == 0) 
      break;
    if (!strncmp(*db_str, db_types[index], strlen(db_types[index]))) {
      *db_str += strlen(db_types[index]);
      return types[index];
    }
  }
  return type;
}

int db_open(const char *file, int type, db_conn_p *db)
{
  int rc = DB_ERROR;
  const char *db_str; 
  db_str = file; 

  type = db_detect_type(&db_str, type);
  /* Switch between implementation */
  switch (type) {
  case DB_SQLITE2:
    rc = db_sqlite2_open(db_str, db);
    break;
  case DB_SQLITE3:
    rc = db_sqlite_open(db_str, db);
    break;
#ifdef HAVE_LIBMYSQLCLIENT
  case DB_MYSQL:
    rc = db_mysql_open(db_str, db);
    break;
#endif
  default:
    csync_fatal("Database type not found. Can't open database%s \n", file);    
    rc = DB_ERROR;
  }
  if ( rc != DB_OK )
    csync_fatal("Can't open database: %s\n", file);
  if (*db)
    (*db)->logger = 0;
  return rc;
}

int db_set_logger(db_conn_p conn, void (*logger)(int lv, const char *fmt, ...)) {
  conn->logger = logger; 
}

void db_close(db_conn_p conn)
{
  if (!conn) 
    return;
  conn->close(conn);
}

const char *db_errmsg(db_conn_p conn) 
{
  if (conn->errmsg)
    return conn->errmsg(conn);
  return "(no error message function available)";
}

int db_exec(db_conn_p conn, const char *sql) {
  return conn->exec(conn, sql);
}

int db_prepare_stmt(db_conn_p conn, const char *sql, db_stmt_p *stmt, char **pptail) {
  return conn->prepare(conn, sql, stmt, pptail);
}

const char *db_stmt_get_column_text(db_stmt_p stmt, int column) {
  return stmt->get_column_text(stmt, column);
}

int db_stmt_get_column_int(db_stmt_p stmt, int column) {
  return stmt->get_column_int(stmt, column);
}

int db_stmt_next(db_stmt_p stmt)
{
  return stmt->next(stmt);
}

int db_stmt_close(db_stmt_p stmt)
{
  return stmt->close(stmt);
}



