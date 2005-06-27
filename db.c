/*
 *  csync2 - cluster synchronisation tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2004, 2005  Clifford Wolf <clifford@clifford.at>
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "csync2.h"
#include <sqlite.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define DEADLOCK_MESSAGE \
	"Detected DB deadlock situation => Terminating (requesting retry).\n"

int db_blocking_mode = 1;
static int db_intransaction = 0;

static sqlite * db = 0;

static int get_dblock_timeout()
{
	return getpid() % 7 + 12;
}

void csync_db_maycommit()
{
	static time_t lastcommit = 0;
	static time_t lastwait = 0;
	static int commitcount = 0;
	static int recursion = 0;
	static int query_counter = 0;
	time_t now;

	if ( !db_blocking_mode || recursion ) return;
	recursion++;

	query_counter++;
	if (query_counter < 100) {
		recursion--;
		return;
	}
	if (query_counter == 100) {
		SQL("BEGIN TRANSACTION", "BEGIN TRANSACTION");
		db_intransaction = 1;
		recursion--;
		return;
	}

	now = time(0);
	if ( !lastcommit ) lastcommit = now;
	if ( !lastwait )   lastwait = now;

	if ( now-lastwait >= 10 || now-lastcommit >= 3 ||
	     commitcount++ >= 1000 ) {
		csync_debug(2, "Commiting current transaction (time=%d, count=%d).\n",
				(int)(now-lastcommit), commitcount-1);
		SQL("COMMIT TRANSACTION", "COMMIT TRANSACTION");
		if ( now-lastwait >= 10 ) {
			lastwait = 0;
			csync_debug(2, "Waiting 2 secs so others can lock the database...\n");
			sleep(2);
		}
		SQL("BEGIN TRANSACTION", "BEGIN TRANSACTION");
		lastcommit = now; commitcount = 0;
	}

	recursion--;
}

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
		"	filename, force, myname, peername,"
		"	UNIQUE ( filename, peername ) ON CONFLICT IGNORE"
		")",
		0, 0, 0);
	sqlite_exec(db,
		"CREATE TABLE hint ("
		"	filename, recursive,"
		"	UNIQUE ( filename, recursive ) ON CONFLICT IGNORE"
		")",
		0, 0, 0);
	sqlite_exec(db,
		"CREATE TABLE action ("
		"	filename, command, logfile,"
		"	UNIQUE ( filename, command ) ON CONFLICT IGNORE"
		")",
		0, 0, 0);
}

void csync_db_close()
{
	static int recursion = 0;
	if (!db || recursion) return;

	recursion++;
	if (db_intransaction)
		SQL("COMMIT TRANSACTION", "COMMIT TRANSACTION");
	sqlite_close(db);
	recursion--;
	db = 0;
}

void csync_db_sql(const char *err, const char *fmt, ...)
{
	char *sql;
	va_list ap;
	int rc, busyc = 0;

	va_start(ap, fmt);
	vasprintf(&sql, fmt, ap);
	va_end(ap);

	csync_debug(2, "SQL: %s\n", sql);

	while (1) {
		rc = sqlite_exec(db, sql, 0, 0, 0);
		if ( rc != SQLITE_BUSY ) break;
		if (busyc++ > get_dblock_timeout()) { db = 0; csync_fatal(DEADLOCK_MESSAGE); }
		csync_debug(2, "Database is busy, sleeping a sec.\n");
		sleep(1);
	}

	if ( rc != SQLITE_OK && err )
		csync_fatal("Database Error: %s [%d]:\n%s\n", err, rc, sql);
	free(sql);

	csync_db_maycommit();
}

void* csync_db_begin(const char *err, const char *fmt, ...)
{
	sqlite_vm *vm;
	char *sql;
	va_list ap;
	int rc, busyc = 0;

	va_start(ap, fmt);
	vasprintf(&sql, fmt, ap);
	va_end(ap);

	csync_debug(2, "SQL: %s\n", sql);

	while (1) {
		rc = sqlite_compile(db, sql, 0, &vm, 0);
		if ( rc != SQLITE_BUSY ) break;
		if (busyc++ > get_dblock_timeout()) { db = 0; csync_fatal(DEADLOCK_MESSAGE); }
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
	int rc, busyc = 0;
	
	csync_debug(4, "Trying to fetch a row from the database.\n");

	while (1) {
		rc = sqlite_step(vm, pN, pazValue, pazColName);
		if ( rc != SQLITE_BUSY ) break;
		if (busyc++ > get_dblock_timeout()) { db = 0; csync_fatal(DEADLOCK_MESSAGE); }
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
	int rc, busyc = 0;
	
	csync_debug(2, "SQL Query finished.\n");

	while (1) {
		rc = sqlite_finalize(vm, 0);
		if ( rc != SQLITE_BUSY ) break;
		if (busyc++ > get_dblock_timeout()) { db = 0; csync_fatal(DEADLOCK_MESSAGE); }
		csync_debug(2, "Database is busy, sleeping a sec.\n");
		sleep(1);
	}

	if ( rc != SQLITE_OK && err )
		csync_fatal("Database Error: %s [%d].\n", err, rc);

	csync_db_maycommit();
}

