
#ifndef DB_MYSQL_H
#define DB_MYSQL_H

/* public */
int   db_mysql_open(const char *file, db_conn_p *conn_p);
/* Private */
void  db_mysql_close(db_conn_p db_conn);
int   db_mysql_exec(db_conn_p conn, const char *sql);
int   db_mysql_prepare(db_conn_p conn, const char *sql, db_stmt_p *stmt_p, char **pptail);
int   db_mysql_stmt_next(db_stmt_p stmt);
const void* db_mysql_stmt_get_column_blob(db_stmt_p stmt, int column);
const char *db_mysql_stmt_get_column_text(db_stmt_p stmt, int column);
// TODO Add error handing
int   db_mysql_stmt_get_column_int(db_stmt_p stmt, int column);
int   db_mysql_stmt_close(db_stmt_p stmt);

#endif
