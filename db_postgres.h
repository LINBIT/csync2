
#ifndef DB_POSTGRES_H
#define DB_POSTGRES_H

/* public */
int   db_postgres_open(const char *file, db_conn_p *conn_p);
/* Private */
void  db_postgres_close(db_conn_p db_conn);
int   db_postgres_exec(db_conn_p conn, const char *sql);
int   db_postgres_prepare(db_conn_p conn, const char *sql, db_stmt_p *stmt_p, char **pptail);
const char *db_postgres_errmsg(db_conn_p db_conn);

int   db_postgres_stmt_next(db_stmt_p stmt);
const void* db_postgres_stmt_get_column_blob(db_stmt_p stmt, int column);
const char *db_postgres_stmt_get_column_text(db_stmt_p stmt, int column);
int   db_postgres_stmt_get_column_int(db_stmt_p stmt, int column);
int   db_postgres_stmt_close(db_stmt_p stmt);
int   db_postgres_upgrade_to_schema(int version);

#endif
