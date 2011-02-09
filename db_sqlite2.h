
#ifndef DB_SQLITE2_H
#define DB_SQLITE2_H

/* public */
int   db_sqlite2_open(const char *file, db_conn_p *conn_p);
/* Private, should not be here  */
void  db_sqlite2_close(db_conn_p db_conn);
int   db_sqlite2_exec(db_conn_p conn, const char *sql);
int   db_sqlite2_prepare(db_conn_p conn, const char *sql, db_stmt_p *stmt_p, char **pptail);
int   db_sqlite2_stmt_next(db_stmt_p stmt);
const char* db_sqlite2_stmt_get_column_text(db_stmt_p stmt, int column);
const void* db_sqlite2_stmt_get_column_blob(db_stmt_p stmt, int column);
int   db_sqlite2_stmt_get_column_int(db_stmt_p stmt, int column);
int   db_sqlite2_stmt_close(db_stmt_p stmt);
int   db_sqlite2_upgrade_to_schema(int version);

#endif
