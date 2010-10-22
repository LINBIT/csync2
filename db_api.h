
#ifndef DB_API_H
#define DB_API_H

#define DB_SQLITE2 1 
#define DB_SQLITE3 2 
#define DB_MYSQL   3
#define DB_PGSQL   4

#define DB_OK                  0
#define DB_ERROR               1
#define DB_BUSY                2
#define DB_NO_CONNECTION       3
#define DB_NO_CONNECTION_REAL  4
#define DB_ROW  100
#define DB_DONE 101

typedef struct db_conn_t *db_conn_p;
typedef struct db_stmt_t *db_stmt_p;

struct db_conn_t {
  void *private;
  int       (*exec)   (db_conn_p conn, const char* exec);
  int       (*prepare)(db_conn_p conn, const char *statement, db_stmt_p *stmt, char **value);
  void      (*close)  (db_conn_p conn);
  void      (*logger) (int lv, const char *fmt, ...);
  const char* (*errmsg) (db_conn_p conn);
  int       (*upgrade_to_schema) (int version);
};

struct db_stmt_t {
  void *private;
  void *private2;
  db_conn_p db;
  const char *    (*get_column_text) (db_stmt_p vmx, int column);
  const void*     (*get_column_blob) (db_stmt_p vmx, int column);
  int             (*get_column_int)  (db_stmt_p vmx, int column);
  int       (*next) (db_stmt_p stmt);
  int       (*close)(db_stmt_p stmt);
};

//struct db_conn *db_conn;

int       db_open(const char *file, int type, db_conn_p *db);
void      db_close(db_conn_p conn);

int       db_exec(db_conn_p conn, const char* exec);
int       db_exec2(db_conn_p conn, const char* exec, void (*callback)(void *, int, int), void *data, const char **err);

int       db_prepare_stmt(db_conn_p conn, const char *statement, db_stmt_p *stmt, char **value);

const char *    db_stmt_get_column_text(db_stmt_p stmt, int column);
int       db_stmt_get_column_int(db_stmt_p  stmt, int column);
int       db_stmt_next (db_stmt_p stmt);
int       db_stmt_close(db_stmt_p stmt);

#endif
