#include "csync2.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <fnmatch.h>

int read_conn_status(FILE * conn, const char *file, const char *host)
{
	char line[4096];
	if ( fgets(line, 4096, conn) ) {
		line[4095] = 0;
		if ( !strncmp(line, "OK (", 4) ) return 0;
	} else 
		strcpy(line, "Connection closed.\n");
	if ( file )
		csync_debug(0, "While syncing file %s:\n", file);
	csync_debug(0, "ERROR from peer %s: %s", host, line);
	csync_error_count++;
	return 1;
}

FILE * connect_to_host(const char * hostname)
{
	struct sockaddr_in sin;
	struct hostent *hp;
	int s;

	hp = gethostbyname(hostname);
	if ( ! hp ) {
		csync_debug(1, "Can't resolve hostname.\n");
		return 0;
	}

	s = socket(hp->h_addrtype, SOCK_STREAM, 0);
	if (s < 0) {
		csync_debug(1, "Can't create socket.\n");
		return 0;
	}

	sin.sin_family = hp->h_addrtype;
	bcopy(hp->h_addr, &sin.sin_addr, hp->h_length);
	sin.sin_port = htons(CSYNC_PORT);

	if (connect(s, (struct sockaddr *)&sin, sizeof (sin)) < 0) {
		csync_debug(1, "Can't connect to remote host.\n");
		return 0;
	}

	return fdopen(s, "r+");
}

void csync_update_file_del(const char *hostname,
		const char *filename, int force, FILE *conn)
{
	const char * key = csync_key(hostname, filename);

	csync_debug(1, "Deleting %s on %s ...\n", filename, hostname);

	if ( !key ) {
		csync_debug(0, "ERROR: No key for %s on %s.n",
				filename, hostname);
		csync_error_count++;
		goto got_error;
	}

	if ( force ) {
		fprintf(conn, "FLUSH %s %s\n", url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	}

	fprintf(conn, "DEL %s %s\n", url_encode(key), url_encode(filename));
	if ( read_conn_status(conn, filename, hostname) ) goto got_error;

	SQL("Remove dirty-file entry.",
		"DELETE FROM dirty WHERE filename = '%s' "
		"AND hostname = '%s'", url_encode(filename),
		url_encode(hostname));
	return;

got_error:
	csync_debug(1, "File stays in dirty state. Try again later...\n");
}

void csync_update_file_mod(const char * hostname,
		const char * filename, int force, FILE * conn)
{
	struct stat st;
	const char * key = csync_key(hostname, filename);

	csync_debug(1, "Updating %s on %s ...\n", filename, hostname);

	if ( !key ) {
		csync_debug(0, "ERROR: No key for %s on %s.\n",
				filename, hostname);
		csync_error_count++;
		goto got_error;
	}

	if ( lstat(filename, &st) != 0 ) {
		csync_debug(0, "ERROR: Cant stat %s.\n", filename);
		csync_error_count++;
		goto got_error;
	}

	if ( force ) {
		fprintf(conn, "FLUSH %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	}

	if ( S_ISREG(st.st_mode) ) {
		fprintf(conn, "PATCH %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
		csync_rs_delta(filename, conn, conn);
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	} else
	if ( S_ISDIR(st.st_mode) ) {
		fprintf(conn, "MKDIR %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	} else
	if ( S_ISCHR(st.st_mode) ) {
		fprintf(conn, "MKCHR %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	} else
	if ( S_ISBLK(st.st_mode) ) {
		fprintf(conn, "MKBLK %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	} else
	if ( S_ISFIFO(st.st_mode) ) {
		fprintf(conn, "MKFIFO %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	} else
	if ( S_ISLNK(st.st_mode) ) {
		char target[1024];
		int rc;
		rc = readlink(filename, target, 1023);
		if ( rc >= 0 ) {
			target[rc]=0;
			fprintf(conn, "MKLINK %s %s %s\n",
					url_encode(key), url_encode(filename),
					url_encode(target));
			if ( read_conn_status(conn, filename, hostname) )
				goto got_error;
		}
	} else
	if ( S_ISSOCK(st.st_mode) ) {
		fprintf(conn, "MKSOCK %s %s\n",
				url_encode(key), url_encode(filename));
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	}

	fprintf(conn, "SETOWN %s %s %d %d\n",
			url_encode(key), url_encode(filename),
			st.st_uid, st.st_gid);
	if ( read_conn_status(conn, filename, hostname) )
		goto got_error;

	if ( !S_ISLNK(st.st_mode) ) {
		fprintf(conn, "SETMOD %s %s %d\n", url_encode(key),
				url_encode(filename), st.st_mode);
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;

		fprintf(conn, "SETIME %s %s %Ld\n",
				url_encode(key), url_encode(filename),
				(long long)st.st_mtime);
		if ( read_conn_status(conn, filename, hostname) )
			goto got_error;
	}

	SQL("Remove dirty-file entry.",
		"DELETE FROM dirty WHERE filename = '%s' "
		"AND hostname = '%s'", url_encode(filename),
		url_encode(hostname));
	return;

got_error:
	csync_debug(1, "File stays in dirty state. Try again later...\n");
}

void csync_update_host(const char * hostname)
{
	FILE * conn;
	struct textlist *tl = 0, *t, *next_t;
	struct textlist *tl_mod = 0, **last_tn=&tl;
	struct stat st;

	csync_debug(1, "Updating host %s ...\n", hostname);

	conn = connect_to_host(hostname);

	if ( !conn ) {
		csync_error_count++;
		csync_debug(1, "ERROR: Connection to remote host failed.\n");
		csync_debug(1, "Host stays in dirty state. "
				"Try again later...\n");
		return;
	}

	SQL_BEGIN("Get files for host from dirty table",
		"SELECT filename, force FROM dirty WHERE hostname = '%s' "
		"ORDER by filename ASC", url_encode(hostname))
	{
		textlist_add(&tl, url_decode(SQL_V[0]), atoi(SQL_V[1]));
	} SQL_END;

	/*
	 * The SQL statement above creates a linked list. Due to the
	 * way the linked list is created, it has the reversed order
	 * of the sql output. This order is good for removing stuff
	 * (deep entries first) but we need to use the original order
	 * for adding things.
	 *
	 * So I added a 2nd linked list for adding and modifying
	 * files: *tl_mod. Whever a file should be added/modified
	 * it's removed in the *tl linked list and moved to that
	 * other linked list.
	 *
	 */
	for (t = tl; t != 0; t = next_t) {
		next_t = t->next;
		if ( !lstat(t->value, &st) != 0 ) {
			*last_tn = next_t;
			t->next = tl_mod;
			tl_mod = t;
		} else {
			csync_update_file_del(hostname,
					t->value, t->intvalue, conn);
			last_tn=&(t->next);
		}
	}

	for (t = tl_mod; t != 0; t = t->next) {
		csync_update_file_mod(hostname,
				t->value, t->intvalue, conn);
	}

	textlist_free(tl_mod);
	textlist_free(tl);

	fprintf(conn, "BYE\n");
	read_conn_status(conn, 0, hostname);
	fclose(conn);
}

void csync_update()
{
	struct textlist *tl = 0, *t;

	SQL_BEGIN("Get hosts from dirty table",
		"SELECT hostname FROM dirty GROUP BY hostname")
	{
		textlist_add(&tl, url_decode(SQL_V[0]), 0);
	} SQL_END;

	for (t = tl; t != 0; t = t->next)
		csync_update_host(t->value);

	textlist_free(tl);
}

