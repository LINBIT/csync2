/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  Copyright (C) 2004 - 2013 LINBIT Information Technologies GmbH
 *  http://www.linbit.com; see also AUTHORS
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "csync2.h"
#include <librsync.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

/* for tmpfile replacement: */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* for MAXPATHLEN */
#include <sys/param.h>


#ifdef __CYGWIN__
#include <w32api/windows.h>
#endif


/* This has been taken from rsync:lib/compat.c */

/**
 * Like strncpy but does not 0 fill the buffer and always null
 * terminates.
 *
 * @param bufsize is the size of the destination buffer.
 *
 * @return index of the terminating byte.
 **/
static size_t strlcpy(char *d, const char *s, size_t bufsize)
{
        size_t len = strlen(s);
        size_t ret = len;
        if (bufsize > 0) {
                if (len >= bufsize)
                        len = bufsize-1;
                memcpy(d, s, len);
                d[len] = 0;
        }
        return ret;
}

/* splits filepath at the last '/', if any, like so:
 *	dirname		basename	filepath
 *	"/"		""		"/"
 *	"/"		"foo"		"/foo"
 * no trailing slash in dirname, unless it is "/".
 *	"/some/path"	""		"/some/path/"
 *	"/some/path"	"foo"		"/some/path/foo"
 *	""		"foo"		"foo"
 *
 * caller needs to supply enough room in dirname and basename
 * to hold the result, or NULL, if not interested.
 */
void split_dirname_basename(char *dirname, char* basename, const char *filepath)
{
	const char *base = strrchr(filepath, '/');
	size_t pathlen, dirlen, baselen;

	/* skip over last slash, if any. */
	base = base ? base + 1 : filepath;

	pathlen = strlen(filepath);
	baselen = strlen(base);
	dirlen = pathlen - baselen;

	/* backtrack trailing slash(es) */
	while (dirlen > 1 && filepath[dirlen-1] == '/')
		--dirlen;

	if (dirname)
		strlcpy(dirname, filepath, dirlen + 1);
	if (basename)
		strlcpy(basename, base, baselen + 1);
}


/* This has been taken from rsync sources: receiver.c */

#define TMPNAME_SUFFIX ".XXXXXX"
#define TMPNAME_SUFFIX_LEN ((int)sizeof TMPNAME_SUFFIX - 1)
#define MAX_UNIQUE_NUMBER 999999
#define MAX_UNIQUE_LOOP 100

/* get_tmpname() - create a tmp filename for a given filename
 *
 * If a tmpdir is defined, use that as the directory to put it in.  Otherwise,
 * the tmp filename is in the same directory as the given name.  Note that
 * there may be no directory at all in the given name!
 *
 * The tmp filename is basically the given filename with a dot prepended, and
 * .XXXXXX appended (for mkstemp() to put its unique gunk in).  We take care
 * to not exceed either the MAXPATHLEN or NAME_MAX, especially the last, as
 * the basename basically becomes 8 characters longer.  In such a case, the
 * original name is shortened sufficiently to make it all fit.
 *
 * If the make_unique arg is True, the XXXXXX string is replaced with a unique
 * string that doesn't exist at the time of the check.  This is intended to be
 * used for creating hard links, symlinks, devices, and special files, since
 * normal files should be handled by mkstemp() for safety.
 *
 * Of course, the only reason the file is based on the original name is to
 * make it easier to figure out what purpose a temp file is serving when a
 * transfer is in progress. */

static int get_tmpname(char *fnametmp, const char *fname)
{
	int maxname, added, length = 0;
	const char *f;
	char *suf;

	static unsigned counter_limit;
	unsigned counter;

	if ((f = strrchr(fname, '/')) != NULL) {
		++f;
		length = f - fname;
		/* copy up to and including the slash */
		strlcpy(fnametmp, fname, length + 1);
	} else
		f = fname;
	fnametmp[length++] = '.';

	/* The maxname value is bufsize, and includes space for the '\0'.
	 * NAME_MAX needs an extra -1 for the name's leading dot. */
	maxname = MIN(MAXPATHLEN - length - TMPNAME_SUFFIX_LEN,
		      NAME_MAX - 1 - TMPNAME_SUFFIX_LEN);

	if (maxname < 1) {
		csync_debug(1, "temporary filename too long: %s\n", fname);
		fnametmp[0] = '\0';
		return 0;
	}

	added = strlcpy(fnametmp + length, f, maxname);
	if (added >= maxname)
		added = maxname - 1;
	suf = fnametmp + length + added;

	if (!counter_limit) {
		counter_limit = (unsigned)getpid() + MAX_UNIQUE_LOOP;
		if (counter_limit > MAX_UNIQUE_NUMBER || counter_limit < MAX_UNIQUE_LOOP)
			counter_limit = MAX_UNIQUE_LOOP;

		counter = counter_limit - MAX_UNIQUE_LOOP;

		/* This doesn't have to be very good because we don't need
		 * to worry about someone trying to guess the values:  all
		 * a conflict will do is cause a device, special file, hard
		 * link, or symlink to fail to be created.  Also: avoid
		 * using mktemp() due to gcc's annoying warning. */
		while (1) {
			snprintf(suf, TMPNAME_SUFFIX_LEN+1, ".%d", counter);
			if (access(fnametmp, 0) < 0)
				break;
			if (++counter >= counter_limit)
				return 0;
		}
	} else
		memcpy(suf, TMPNAME_SUFFIX, TMPNAME_SUFFIX_LEN+1);

	return 1;
}

/*
 * Recursively creates the given path, with the given mode
 * Note that path argument is not directory name here but rather
 * a path to a file that you are going to create after calling mkpath().
 * Works with relative paths as well.
 * Shamelessly copied from
 * Stackoverlow.com#http://stackoverflow.com/questions/2336242/recursive-mkdir-system-call-on-unix
 * Returns: 0 on success and -1 on error
 */
int mkpath(const char *path, mode_t mode) {
	char temp[MAXPATHLEN];
	char *remaining;

	if(!mode) {
		mode=S_IRWXU;
	}
	if(!path){
		csync_debug(2,"invalid path");
		return -1;
	}

	strlcpy(temp,path,strlen(path));
	csync_debug(1,"mkpath full path: %s",temp);
	for( remaining=strchr(temp+1, '/'); remaining!=NULL; remaining=strchr(remaining+1, '/') ){
		*remaining='\0';
		if(mkdir(temp, mode)==-1) { //strchr keeps the parent in temp and child[ren] in remaining
			if(errno != EEXIST) {
				*remaining='/';
				csync_debug(1,"error occured while creating path %s; cause : %s",temp,strerror(errno));
				return -1;
			}
		}
		csync_debug(1,"mkdir parent dir: %s",temp);
		*remaining='/';
	}
	return 0;
}


/* Returns open file handle for a temp file that resides in the
   same directory as file fname. The file must be removed after
   usage.
*/

static FILE *open_temp_file(char *fnametmp, const char *fname)
{
	FILE *f;
	int fd;

	if (get_tmpname(fnametmp, fname) == 0) {
		csync_debug(1, "ERROR: Couldn't find tempname for file %s\n", fname);
		return NULL;
	}

	f = NULL;
	fd = open(fnametmp, O_CREAT | O_EXCL | O_RDWR, S_IWUSR | S_IRUSR);
	if (fd >= 0) {
		f = fdopen(fd, "wb+");
			/* not unlinking since rename wouldn't work then */
	}
	if (fd < 0 || !f) {
		csync_debug(1, "ERROR: Could not open result from tempnam(%s)!\n", fnametmp);
		return NULL;
	}

	return f;
}

void csync_send_file(FILE *in)
{
	char buffer[512];
	int rc, chunk;
	long size;

	fflush(in);
	size = ftell(in);
	rewind(in);

	conn_printf("octet-stream %ld\n", size);

	while ( size > 0 ) {
		chunk = size > 512 ? 512 : size;
		rc = fread(buffer, 1, chunk, in);

		if ( rc <= 0 )
			csync_fatal("Read-error while sending data.\n");
		chunk = rc;

		rc = conn_write(buffer, chunk);
		if ( rc != chunk )
			csync_fatal("Write-error while sending data.\n");

		size -= chunk;
	}
}

int csync_recv_file(FILE *out)
{
	char buffer[512];
	int rc, chunk;
	long size;

	if ( !conn_gets(buffer, 100) || sscanf(buffer, "octet-stream %ld\n", &size) != 1 ) {
		if (!strcmp(buffer, "ERROR\n")) { errno=EIO; return -1; }
		csync_fatal("Format-error while receiving data.\n");
	}

	csync_debug(3, "Receiving %ld bytes ..\n", size);

	while ( size > 0 ) {
		chunk = size > 512 ? 512 : size;
		rc = conn_read(buffer, chunk);

		if ( rc <= 0 )
			csync_fatal("Read-error while receiving data.\n");
		chunk = rc;

		rc = fwrite(buffer, chunk, 1, out);
		if ( rc != 1 )
			csync_fatal("Write-error while receiving data.\n");

		size -= chunk;
		csync_debug(3, "Got %d bytes, %ld bytes left ..\n",
				chunk, size);
	}

	fflush(out);
	rewind(out);
	return 0;
}

int csync_rs_check(const char *filename, int isreg)
{
	FILE *basis_file = 0, *sig_file = 0;
	char buffer1[512], buffer2[512];
	int rc, chunk, found_diff = 0;
	int backup_errno;
	rs_stats_t stats;
	rs_result result;
	long size;
	char tmpfname[MAXPATHLEN];

	csync_debug(3, "Csync2 / Librsync: csync_rs_check('%s', %d [%s])\n",
		    filename, isreg, isreg ? "regular file" : "non-regular file");

	csync_debug(3, "Opening basis_file and sig_file..\n");

	sig_file = open_temp_file(tmpfname, prefixsubst(filename));
	if (!sig_file)
		goto io_error;
	if (unlink(tmpfname) < 0)
		goto io_error;

	basis_file = fopen(prefixsubst(filename), "rb");
	if (!basis_file) {	/* ?? why a tmp file? */
		basis_file = open_temp_file(tmpfname, prefixsubst(filename));
		if (!basis_file)
			goto io_error;
		if (unlink(tmpfname) < 0)
			goto io_error;
	}

	if (isreg) {
		csync_debug(3, "Running rs_sig_file() from librsync....\n");
		result = rs_sig_file(basis_file, sig_file, RS_DEFAULT_BLOCK_LEN, RS_DEFAULT_STRONG_LEN, &stats);
		if (result != RS_DONE) {
			csync_debug(0, "Internal error from rsync library!\n");
			goto error;
		}
	}

	fclose(basis_file);
	basis_file = 0;

	{
		char line[100];
		csync_debug(3, "Reading signature size from peer....\n");
		if (!conn_gets(line, 100) || sscanf(line, "octet-stream %ld\n", &size) != 1)
			csync_fatal("Format-error while receiving data.\n");
	}

	fflush(sig_file);
	if (size != ftell(sig_file)) {
		csync_debug(2, "Signature size differs: local=%d, peer=%d\n", ftell(sig_file), size);
		found_diff = 1;
	}
	rewind(sig_file);

	csync_debug(3, "Receiving %ld bytes ..\n", size);

	while (size > 0) {
		chunk = size > 512 ? 512 : size;
		rc = conn_read(buffer1, chunk);

		if (rc <= 0)
			csync_fatal("Read-error while receiving data.\n");
		chunk = rc;

		if (fread(buffer2, chunk, 1, sig_file) != 1) {
			csync_debug(2, "Found EOF in local sig file.\n");
			found_diff = 1;
		}
		if (memcmp(buffer1, buffer2, chunk)) {
			csync_debug(2, "Found diff in sig at -%d:-%d\n", size, size - chunk);
			found_diff = 1;
		}

		size -= chunk;
		csync_debug(3, "Got %d bytes, %ld bytes left ..\n", chunk, size);
	}

	csync_debug(3, "File has been checked successfully (%s).\n", found_diff ? "difference found" : "files are equal");
	fclose(sig_file);
	return found_diff;

io_error:
	csync_debug(0, "I/O Error '%s' in rsync-check: %s\n", strerror(errno), prefixsubst(filename));

error:
	backup_errno = errno;
	if (basis_file)
		fclose(basis_file);
	if (sig_file)
		fclose(sig_file);
	errno = backup_errno;
	return -1;
}

void csync_rs_sig(const char *filename)
{
	FILE *basis_file = 0, *sig_file = 0;
	rs_stats_t stats;
	rs_result result;
	char tmpfname[MAXPATHLEN];

	csync_debug(3, "Csync2 / Librsync: csync_rs_sig('%s')\n", filename);

	csync_debug(3, "Opening basis_file and sig_file..\n");

	sig_file = open_temp_file(tmpfname, prefixsubst(filename));
	if ( !sig_file ) goto io_error;
	if (unlink(tmpfname) < 0) goto io_error;

	basis_file = fopen(prefixsubst(filename), "rb");
	if ( !basis_file ) basis_file = fopen("/dev/null", "rb");

	csync_debug(3, "Running rs_sig_file() from librsync..\n");
	result = rs_sig_file(basis_file, sig_file,
			RS_DEFAULT_BLOCK_LEN, RS_DEFAULT_STRONG_LEN, &stats);
	if (result != RS_DONE)
		csync_fatal("Got an error from librsync, too bad!\n");

	csync_debug(3, "Sending sig_file to peer..\n");
	csync_send_file(sig_file);

	csync_debug(3, "Signature has been created successfully.\n");
	fclose(basis_file);
	fclose(sig_file);

	return;

io_error:
	csync_debug(0, "I/O Error '%s' in rsync-sig: %s\n",
			strerror(errno), prefixsubst(filename));

	if (basis_file) fclose(basis_file);
	if (sig_file) fclose(sig_file);
}



int csync_rs_delta(const char *filename)
{
	FILE *sig_file = 0, *new_file = 0, *delta_file = 0;
	rs_result result;
	rs_signature_t *sumset;
	rs_stats_t stats;
	char tmpfname[MAXPATHLEN];

	csync_debug(3, "Csync2 / Librsync: csync_rs_delta('%s')\n", filename);

	csync_debug(3, "Receiving sig_file from peer..\n");
	sig_file = open_temp_file(tmpfname, prefixsubst(filename));
	if ( !sig_file ) goto io_error;
	if (unlink(tmpfname) < 0) goto io_error;

	if ( csync_recv_file(sig_file) ) {
		fclose(sig_file);
		return -1;
	}
	result = rs_loadsig_file(sig_file, &sumset, &stats);
	if (result != RS_DONE)
		csync_fatal("Got an error from librsync, too bad!\n");
	fclose(sig_file);

	csync_debug(3, "Opening new_file and delta_file..\n");
	new_file = fopen(prefixsubst(filename), "rb");
	if ( !new_file ) {
		int backup_errno = errno;
		const char *errstr = strerror(errno);
		csync_debug(0, "I/O Error '%s' while %s in rsync-delta: %s\n",
				errstr, "opening data file for reading", filename);
		conn_printf("%s\n", errstr);
		fclose(new_file);
		errno = backup_errno;
		return -1;
	}

	delta_file = open_temp_file(tmpfname, prefixsubst(filename));
	if ( !delta_file ) goto io_error;
	if (unlink(tmpfname) < 0) goto io_error;

	csync_debug(3, "Running rs_build_hash_table() from librsync..\n");
	result = rs_build_hash_table(sumset);
	if (result != RS_DONE)
		csync_fatal("Got an error from librsync, too bad!\n");

	csync_debug(3, "Running rs_delta_file() from librsync..\n");
	result = rs_delta_file(sumset, new_file, delta_file, &stats);
	if (result != RS_DONE)
		csync_fatal("Got an error from librsync, too bad!\n");

	csync_debug(3, "Sending delta_file to peer..\n");
	csync_send_file(delta_file);

	csync_debug(3, "Delta has been created successfully.\n");
	rs_free_sumset(sumset);
	fclose(delta_file);
	fclose(new_file);

	return 0;

io_error:
	csync_debug(0, "I/O Error '%s' in rsync-delta: %s\n",
			strerror(errno), prefixsubst(filename));

	if (new_file) fclose(new_file);
	if (delta_file) fclose(delta_file);
	if (sig_file) fclose(sig_file);

	return -1;
}

int csync_rs_patch(const char *filename)
{
	FILE *basis_file = 0, *delta_file = 0, *new_file = 0;
	int backup_errno;
	rs_stats_t stats;
	rs_result result;
	char *errstr = "?";
	char tmpfname[MAXPATHLEN], newfname[MAXPATHLEN];

	csync_debug(3, "Csync2 / Librsync: csync_rs_patch('%s')\n", filename);

	csync_debug(3, "Receiving delta_file from peer..\n");
	delta_file = open_temp_file(tmpfname, prefixsubst(filename));
	if ( !delta_file ) { errstr="creating delta temp file"; goto io_error; }
	if (unlink(tmpfname) < 0) { errstr="removing delta temp file"; goto io_error; }
	if ( csync_recv_file(delta_file) ) goto error;

	csync_debug(3, "Opening to be patched file on local host..\n");
	basis_file = fopen(prefixsubst(filename), "rb");
	if ( !basis_file ) {
		basis_file = open_temp_file(tmpfname, prefixsubst(filename));
		if ( !basis_file ) { errstr="opening data file for reading"; goto io_error; }
		if (unlink(tmpfname) < 0) { errstr="removing data temp file"; goto io_error; }
	}

	csync_debug(3, "Opening temp file for new data on local host..\n");
	new_file = open_temp_file(newfname, prefixsubst(filename));
	if ( !new_file ) { errstr="creating new data temp file"; goto io_error; }

	csync_debug(3, "Running rs_patch_file() from librsync..\n");
	result = rs_patch_file(basis_file, delta_file, new_file, &stats);
	if (result != RS_DONE) {
		csync_debug(0, "Internal error from rsync library!\n");
		goto error;
	}

	csync_debug(3, "Renaming tmp file to data file..\n");
	fclose(basis_file);

#ifdef __CYGWIN__

/* TODO: needed? */
	// This creates the file using the native windows API, bypassing
	// the cygwin wrappers and so making sure that we do not mess up the
	// permissions..
	{
		char winfilename[MAX_PATH];
		HANDLE winfh;

		cygwin_conv_to_win32_path(prefixsubst(filename), winfilename);

		winfh = CreateFile(TEXT(winfilename),
				GENERIC_WRITE,          // open for writing
				0,                      // do not share
				NULL,                   // default security
				CREATE_ALWAYS,          // overwrite existing
				FILE_ATTRIBUTE_NORMAL | // normal file
				FILE_FLAG_OVERLAPPED,   // asynchronous I/O
				NULL);                  // no attr. template

		if (winfh == INVALID_HANDLE_VALUE) {
			csync_debug(0, "Win32 I/O Error %d in rsync-patch: %s\n",
					(int)GetLastError(), winfilename);
			errno = EACCES;
			goto error;
		}
		CloseHandle(winfh);
	}
#endif

	if (rename(newfname, prefixsubst(filename)) < 0) { errstr="renaming tmp file to to be patched file"; goto io_error; }

	csync_debug(3, "File has been patched successfully.\n");
	fclose(delta_file);
	fclose(new_file);

	return 0;

io_error:
	csync_debug(0, "I/O Error '%s' while %s in rsync-patch: %s\n",
			strerror(errno), errstr, prefixsubst(filename));

error:;
	backup_errno = errno;
	if ( delta_file ) fclose(delta_file);
	if ( basis_file ) fclose(basis_file);
	if ( new_file )   fclose(new_file);
	errno = backup_errno;
	return -1;
}

