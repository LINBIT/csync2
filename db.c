#include "csync2.h"
#include <sqlite.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

static sqlite * db = 0;

void csync_db_open(const char * file)
{
	db = sqlite_open(file, 0, 0);
	if ( db == 0 )
		csync_fatal("Can't open database: %s\n", file);

	/* ignore errors on table creation */
	sqlite_exec(db,
		"CREATE TABLE file ("
		"	filename, checktxt,"
		"	UNIQUE ( filename ) ON CONFLICT REPLACE"
		")",
		0, 0, 0);
	sqlite_exec(db,
		"CREATE TABLE dirty ("
		"	filename, force, hostname,"
		"	UNIQUE ( filename, hostname ) ON CONFLICT IGNORE"
		")",
		0, 0, 0);
	sqlite_exec(db,
		"CREATE TABLE hint ("
		"	filename, recursive,"
		"	UNIQUE ( filename, recursive ) ON CONFLICT IGNORE"
		")",
		0, 0, 0);
}

void csync_db_close()
{
	sqlite_close(db);
	db = 0;
}

void csync_db_sql(const char *err, const char *fmt, ...)
{
	char *sql;
	va_list ap;
	int rc;

	va_start(ap, fmt);
	vasprintf(&sql, fmt, ap);
	va_end(ap);

	csync_debug(2, "SQL: %s\n", sql);

	while (1) {
		rc = sqlite_exec(db, sql, 0, 0, 0);
		if ( rc != SQLITE_BUSY ) break;
		csync_debug(2, "Database is busy, sleeping a sec.\n");
		sleep(1);
	}

	if ( rc != SQLITE_OK && err )
		csync_fatal("Database Error: %s [%d]:\n%s\n", err, rc, sql);
	free(sql);
}

void* csync_db_begin(const char *err, const char *fmt, ...)
{
	sqlite_vm *vm;
	char *sql;
	va_list ap;
	int rc;

	va_start(ap, fmt);
	vasprintf(&sql, fmt, ap);
	va_end(ap);

	csync_debug(2, "SQL: %s\n", sql);

	while (1) {
		rc = sqlite_compile(db, sql, 0, &vm, 0);
		if ( rc != SQLITE_BUSY ) break;
		csync_debug(2, "Database is busy, sleeping a sec.\n");
		sleep(1);
	}

	if ( rc != SQLITE_OK && err )
		csync_fatal("Database Error: %s [%d]:\n%s\n", err, rc, sql);
	free(sql);

	return vm;
}

int csync_db_next(void *vmx, const char *err,
		int *pN, const char ***pazValue, const char ***pazColName)
{
	sqlite_vm *vm = vmx;
	int rc;
	
	csync_debug(2, "Trying to fetch a row from the database.\n");

	while (1) {
		rc = sqlite_step(vm, pN, pazValue, pazColName);
		if ( rc != SQLITE_BUSY ) break;
		csync_debug(2, "Database is busy, sleeping a sec.\n");
		sleep(1);
	}

	if ( rc != SQLITE_OK && rc != SQLITE_ROW &&
	     rc != SQLITE_DONE && err )
		csync_fatal("Database Error: %s [%d].\n", err, rc);

	return rc == SQLITE_ROW;
}

void csync_db_fin(void *vmx, const char *err)
{
	sqlite_vm *vm = vmx;
	int rc;
	
	csync_debug(2, "SQL Query finished.\n");

	while (1) {
		rc = sqlite_finalize(vm, 0);
		if ( rc != SQLITE_BUSY ) break;
		csync_debug(2, "Database is busy, sleeping a sec.\n");
		sleep(1);
	}

	if ( rc != SQLITE_OK && err )
		csync_fatal("Database Error: %s [%d].\n", err, rc);
}

