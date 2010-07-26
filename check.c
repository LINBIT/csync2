/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2004, 2005, 2006  Clifford Wolf <clifford@clifford.at>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>


#ifdef __CYGWIN__

#include <w32api/windows.h>

/* This does only check the case of the last filename element. But that should
 * be OK for us now...
 */
int csync_cygwin_case_check(const char *filename)
{
	if (!strcmp(filename, "/cygdrive"))
		goto check_ok;
	if (!strncmp(filename, "/cygdrive/", 10) && strlen(filename) == 11)
		goto check_ok;

	char winfilename[MAX_PATH];
	cygwin_conv_to_win32_path(filename, winfilename);

	int winfilename_len = strlen(winfilename);
	int found_file_len;
	HANDLE found_file_handle;
	WIN32_FIND_DATA fd;

	/* See if we can find this file. */
	found_file_handle = FindFirstFile(winfilename, &fd);
	if (found_file_handle == INVALID_HANDLE_VALUE)
		goto check_failed;
	FindClose(found_file_handle);

	found_file_len = strlen(fd.cFileName);

	/* This should never happen. */
	if (found_file_len > winfilename_len)
		goto check_failed;

	if (strcmp(winfilename + winfilename_len - found_file_len, fd.cFileName))
		goto check_failed;

check_ok:
	csync_debug(3, "Cygwin/Win32 filename case check ok: %s (%s)\n", winfilename, filename);
	return 1;

check_failed:
	csync_debug(2, "Cygwin/Win32 filename case check failed: %s (%s)\n", winfilename, filename);
	return 0;
}

#endif /* __CYGWIN__ */

void csync_hint(const char *file, int recursive)
{
	SQL("Adding Hint",
		"INSERT INTO hint (filename, recursive) "
		"VALUES ('%s', %d)", url_encode(file), recursive);
}

void csync_mark(const char *file, const char *thispeer, const char *peerfilter)
{
	struct peer *pl = csync_find_peers(file, thispeer);
	int pl_idx;

	csync_schedule_commands(file, thispeer == 0);

	if ( ! pl ) {
		csync_debug(2, "Not in one of my groups: %s (%s)\n",
				file, thispeer ? thispeer : "NULL");
		return;
	}

	csync_debug(1, "Marking file as dirty: %s\n", file);
	for (pl_idx=0; pl[pl_idx].peername; pl_idx++)
		if (!peerfilter || !strcmp(peerfilter, pl[pl_idx].peername))
			SQL("Marking File Dirty",
				"%s INTO dirty (filename, force, myname, peername) "
				"VALUES ('%s', %s, '%s', '%s')",
				csync_new_force ? "REPLACE" : "INSERT",
				url_encode(file),
				csync_new_force ? "1" : "0",
				url_encode(pl[pl_idx].myname),
				url_encode(pl[pl_idx].peername));

	free(pl);
}

/* return 0 if path does not contain any symlinks */
int csync_check_pure(const char *filename)
{
#ifdef __CYGWIN__
	// For some reason or another does this function __kills__
	// the performance when using large directories with cygwin.
	// And there are no symlinks in windows anyways..
	if (!csync_lowercyg_disable)
		return 0;
#endif

	struct stat sbuf;
	int i=0;

	while (filename[i]) i++;

	{ /* new block for myfilename[] */
		char myfilename[i+1];
		memcpy(myfilename, filename, i+1);
		while (1) {
			while (myfilename[i] != '/')
				if (--i <= 0) return 0;
			myfilename[i]=0;
			if ( lstat_strict(prefixsubst(myfilename), &sbuf) || S_ISLNK(sbuf.st_mode) ) return 1;
		}
	}
}

void csync_check_del(const char *file, int recursive, int init_run)
{
	char *where_rec = "";
	struct textlist *tl = 0, *t;
	struct stat st;

	if ( recursive ) {
		if ( !strcmp(file, "/") )
			asprintf(&where_rec, "or 1");
		else
			asprintf(&where_rec, "UNION ALL SELECT filename from file where filename > '%s/' "
					"and filename < '%s0'",
					url_encode(file), url_encode(file));
	}

	SQL_BEGIN("Checking for removed files",
			"SELECT filename from file where "
			"filename = '%s' %s ORDER BY filename", url_encode(file), where_rec)
	{
		const char *filename = url_decode(SQL_V(0));

		if (!csync_match_file(filename))
			continue;

		if ( lstat_strict(prefixsubst(filename), &st) != 0 || csync_check_pure(filename) )
			textlist_add(&tl, filename, 0);
	} SQL_END;

	for (t = tl; t != 0; t = t->next) {
		if (!init_run) csync_mark(t->value, 0, 0);
		SQL("Removing file from DB. It isn't with us anymore.",
		    "DELETE FROM file WHERE filename = '%s'",
		    url_encode(t->value));
	}

	textlist_free(tl);

	if ( recursive )
		free(where_rec);
}

int csync_check_mod(const char *file, int recursive, int ignnoent, int init_run)
{
	int check_type = csync_match_file(file);
	int dirdump_this = 0, dirdump_parent = 0;
	struct dirent **namelist;
	int n, this_is_dirty = 0;
	const char *checktxt;
	struct stat st;

	if (*file != '%') {
		struct csync_prefix *p;
		for (p = csync_prefix; p; p = p->next)
		{
			if (!p->path)
				continue;

			if (!strcmp(file, p->path)) {
				char new_file[strlen(p->name) + 3];
				sprintf(new_file, "%%%s%%", p->name);
				csync_check_mod(new_file, recursive, ignnoent, init_run);
				continue;
			}

			if (check_type < 1) {
				int file_len = strlen(file);
				int path_len = strlen(p->path);

				if (file_len < path_len && p->path[file_len] == '/' &&
				    !strncmp(file, p->path, file_len))
					check_type = 1;
			}
		}
	}

	if ( check_type>0 && lstat_strict(prefixsubst(file), &st) != 0 ) {
		if ( ignnoent ) return 0;
		csync_fatal("This should not happen: "
				"Can't stat %s.\n", file);
	}

	switch ( check_type )
	{
	case 2:
		csync_debug(2, "Checking %s.\n", file);
		checktxt = csync_genchecktxt(&st, file, 0);

		if (csync_compare_mode)
			printf("%s\n", file);

		SQL_BEGIN("Checking File",
			"SELECT checktxt FROM file WHERE "
			"filename = '%s'", url_encode(file))
		{
			if ( !csync_cmpchecktxt(checktxt,
						url_decode(SQL_V(0))) ) {
				csync_debug(2, "File has changed: %s\n", file);
				this_is_dirty = 1;
			}
		} SQL_FIN {
			if ( SQL_COUNT == 0 ) {
				csync_debug(2, "New file: %s\n", file);
				this_is_dirty = 1;
			}
		} SQL_END;

		if ( this_is_dirty && !csync_compare_mode ) {
			SQL("Adding or updating file entry",
			    "INSERT INTO file (filename, checktxt) "
			    "VALUES ('%s', '%s')",
			    url_encode(file), url_encode(checktxt));
			if (!init_run) csync_mark(file, 0, 0);
		}
		dirdump_this = 1;
		dirdump_parent = 1;
		/* fall thru */
	case 1:
		if ( !recursive ) break;
		if ( !S_ISDIR(st.st_mode) ) break;
		csync_debug(2, "Checking %s%s* ..\n",
				file, !strcmp(file, "/") ? "" : "/");
		n = scandir(prefixsubst(file), &namelist, 0, alphasort);
		if (n < 0) {
			csync_debug(0, "%s in scandir: %s (%s)\n",
				strerror(errno), prefixsubst(file), file);
			csync_error_count++;
		} else {
			while(n--) {
				on_cygwin_lowercase(namelist[n]->d_name);
				if ( strcmp(namelist[n]->d_name, ".") &&
						strcmp(namelist[n]->d_name, "..") ) {
					char fn[strlen(file)+
						strlen(namelist[n]->d_name)+2];
					sprintf(fn, "%s/%s",
						!strcmp(file, "/") ? "" : file,
						namelist[n]->d_name);
					if (csync_check_mod(fn, recursive, 0, init_run))
						dirdump_this = 1;
				}
				free(namelist[n]);
			}
			free(namelist);
		}
		if ( dirdump_this && csync_dump_dir_fd >= 0 ) {
			int written = 0, len = strlen(file)+1;
			while (written < len) {
				int rc = write(csync_dump_dir_fd, file+written, len-written);
				if (rc <= 0)
					csync_fatal("Error while writing to dump_dir_fd %d: %s\n",
							csync_dump_dir_fd, strerror(errno));
				written += rc;
			}
		}
		break;
	default:
		csync_debug(2, "Don't check at all: %s\n", file);
		break;
	}

	return dirdump_parent;
}

void csync_check(const char *filename, int recursive, int init_run)
{
#if __CYGWIN__
	if (!strcmp(filename, "/")) {
		filename = "/cygdrive";
	}
#endif
	struct csync_prefix *p = csync_prefix;

	csync_debug(2, "Running%s check for %s ...\n",
			recursive ? " recursive" : "", filename);

	if (!csync_compare_mode)
		csync_check_del(filename, recursive, init_run);

	csync_check_mod(filename, recursive, 1, init_run);

	if (*filename == '/')
		while (p) {
			if (p->path) {
				int p_len = strlen(p->path);
				int f_len = strlen(filename);

				if (p_len <= f_len && !strncmp(p->path, filename, p_len) &&
						(filename[p_len] == '/' || !filename[p_len])) {
					char new_filename[strlen(p->name) + strlen(filename+p_len) + 10];
					sprintf(new_filename, "%%%s%%%s", p->name, filename+p_len);

					csync_debug(2, "Running%s check for %s ...\n",
							recursive ? " recursive" : "", new_filename);

					if (!csync_compare_mode)
						csync_check_del(new_filename, recursive, init_run);
					csync_check_mod(new_filename, recursive, 1, init_run);
				}
			}
			p = p->next;
		}
}

