
#ifndef DB_SQLITE_H
#define DB_SQLITE_H

/* public */
int   db_sqlite_open(const char *file, db_conn_p *conn_p);
/* Private */
void  db_sqlite_close(db_conn_p db_conn);
int   db_sqlite_exec(db_conn_p conn, const char *sql);
int   db_sqlite_prepare(db_conn_p conn, const char *sql, db_stmt_p *stmt_p, char **pptail);
int   db_sqlite_stmt_next(db_stmt_p stmt);
const char* db_sqlite_stmt_get_column_text(db_stmt_p stmt, int column);
const void* db_sqlite_stmt_get_column_blob(db_stmt_p stmt, int column);
int   db_sqlite_stmt_get_column_int(db_stmt_p stmt, int column);
int   db_sqlite_stmt_close(db_stmt_p stmt);
const char *db_sqlite_errmsg(db_conn_p conn);
int db_sqlite_upgrade_to_schema(int version);

#endif
