/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  Copyright (C) 2004 - 2015 LINBIT Information Technologies GmbH
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
#include <utime.h>
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
#ifndef HAVE_STRLCPY
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
#endif

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

/* This has been taken from rsync sources: syscall.c */

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* like mkstemp but forces permissions */
int do_mkstemp(char *template, mode_t perms)
{
	perms |= S_IWUSR;

#if defined HAVE_SECURE_MKSTEMP && defined HAVE_FCHMOD && (!defined HAVE_OPEN64 || defined HAVE_MKSTEMP64)
	{
		int fd = mkstemp(template);
		if (fd == -1)
			return -1;
		if (fchmod(fd, perms) != 0) {
			int errno_save = errno;
			close(fd);
			unlink(template);
			errno = errno_save;
			return -1;
		}
#if defined HAVE_SETMODE && O_BINARY
		setmode(fd, O_BINARY);
#endif
		return fd;
	}
#else
	if (!mktemp(template))
		return -1;
	return open(template, O_RDWR|O_EXCL|O_CREAT | O_BINARY, perms);
#endif
}


/* define the order in which directories are tried when creating temp files */
static int next_tempdir(char **dir, unsigned int stage)
{
	static char *dirs_to_try[] = {
		NULL /* csync_tempdir */,
		NULL /* stays NULL, same dir as input name */,
		NULL /* getenv("TMPDIR") */,
		P_tmpdir,
		"/tmp",
	};
	static int n_dirs;
	int i;

	if (!n_dirs) {
		n_dirs = sizeof(dirs_to_try)/sizeof(dirs_to_try[0]);
		dirs_to_try[0] = csync_tempdir;
		dirs_to_try[2] = getenv("TMPDIR");
		for (i = 0; i < n_dirs; i++) {
			struct stat sbuf;
			int ret;
			if (!dirs_to_try[i])
				continue;
			if (!dirs_to_try[i][0]) {
				/* drop "" */
				dirs_to_try[i] = NULL;
				continue;
			}
			ret = stat(dirs_to_try[i], &sbuf);
			if (ret || !S_ISDIR(sbuf.st_mode)) {
				csync_debug(1, "dropping tempdir candidate '%s': not a directory\n",
					dirs_to_try[i]);
				dirs_to_try[i] = NULL;
			}
		}
	}

	/* skip this stage, if previous stages have been equal. */
	for (; stage < n_dirs; stage++) {
		if (dirs_to_try[stage] && !dirs_to_try[stage][0])
			continue;

		for (i = 0; i < stage; i++) {
			if (dirs_to_try[i] == dirs_to_try[stage])
				break;
			if (!dirs_to_try[i] || !dirs_to_try[stage])
				continue;
			if (!strcmp(dirs_to_try[i], dirs_to_try[stage]))
				break;
		}
		if (i == stage) {
			*dir = dirs_to_try[stage];
			return stage+1;
		}
	}
	return -1;
}

/* This has been taken from rsync sources: receiver.c,
 * and adapted: dropped the "make_unique" parameter,
 * as in our use case it is always false.
 *
 * Added tempdir parameter,
 * so we can try several dirs before giving up.
 */

#define TMPNAME_SUFFIX ".XXXXXX"
#define TMPNAME_SUFFIX_LEN ((int)sizeof TMPNAME_SUFFIX - 1)

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
 * Of course, the only reason the file is based on the original name is to
 * make it easier to figure out what purpose a temp file is serving when a
 * transfer is in progress. */
static int get_tmpname(char *fnametmp, const char *tempdir, const char *fname)
{
	int maxname, length = 0;
	const char *f;
	char *suf;

	if (tempdir) {
		/* Note: this can't overflow, so the return value is safe */
		length = strlcpy(fnametmp, tempdir, MAXPATHLEN - 2);
		fnametmp[length++] = '/';
	}

	if ((f = strrchr(fname, '/')) != NULL) {
		++f;
		if (!tempdir) {
			length = f - fname;
			/* copy up to and including the slash */
			strlcpy(fnametmp, fname, length + 1);
		}
	} else
		f = fname;
	if (*f == '.') /* avoid an extra leading dot for OS X's sake */
		f++;
	fnametmp[length++] = '.';

	/* The maxname value is bufsize, and includes space for the '\0'.
	 * NAME_MAX needs an extra -1 for the name's leading dot. */
	maxname = MIN(MAXPATHLEN - length - TMPNAME_SUFFIX_LEN,
		      NAME_MAX - 1 - TMPNAME_SUFFIX_LEN);

	if (maxname < 0) {
		csync_debug(1, "temporary filename too long: %s\n", fname);
		fnametmp[0] = '\0';
		return 0;
	}

	if (maxname) {
		int added = strlcpy(fnametmp + length, f, maxname);
		if (added >= maxname)
			added = maxname - 1;
		suf = fnametmp + length + added;

		/* Trim any dangling high-bit chars if the first-trimmed char (if any) is
		 * also a high-bit char, just in case we cut into a multi-byte sequence.
		 * We are guaranteed to stop because of the leading '.' we added. */
		if ((int)f[added] & 0x80) {
			while ((int)suf[-1] & 0x80)
				suf--;
		}
		/* trim one trailing dot before our suffix's dot */
		if (suf[-1] == '.')
			suf--;
	} else
		suf = fnametmp + length - 1; /* overwrite the leading dot with suffix's dot */

	memcpy(suf, TMPNAME_SUFFIX, TMPNAME_SUFFIX_LEN+1);

	return 1;
}

/* Returns open file handle for a temp file that resides in the
 * csync_tempdir (if specified), or
 * the same directory as fname.
 * If tempfile creation was not possible, before giving up,
 * TMPDIR, P_tmpdir, or /tmp are also tried, in that order.
 * The file must be removed after usage, or renamed into place.
 */

static FILE* open_temp_file(char *fnametmp, const char *fname)
{
	FILE *f = NULL;
	char *dir = NULL;
	int fd;
	int i = 0;

	do {
		if (i > 0)
			csync_debug(3, "mkstemp %s failed: %s\n", fnametmp, strerror(errno));

		i = next_tempdir(&dir, i);
		if (i < 0)
			return NULL;

		if (!get_tmpname(fnametmp, dir, fname))
			return NULL;

		fd = do_mkstemp(fnametmp, S_IRUSR|S_IWUSR);
	} while (fd < 0 && errno == ENOENT);

	if (fd >= 0) {
		f = fdopen(fd, "wb+");
			/* not unlinking since rename wouldn't work then */
	}

	if (fd < 0 || !f) {
		csync_debug(1, "mkstemp %s failed: %s\n", fnametmp, strerror(errno));
		return NULL;
	}

	return f;
}

/* FIXME ftell? long? seriously?
 *
 * Then again, it is only a sigfile, so the base file size would need to be
 * positively huge to have the size of the signature overflow a 32 bit LONG_MAX.
 * In which case csync2 would be the wrong tool anyways.
 */

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
	if (size < 0) { errno=EIO; return -1; }

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

/*
 * Return:
 * 	0, *sig_file == NULL: base file does not exist, empty sig.
 * 	0, *sig_file != NULL: sig_file contains the sig
 *     -1, *sig_file == NULL: "IO Error"
 *     -1, *sig_file != NULL: librsync error
 */
int csync_rs_sigfile(const char *filename, FILE **sig_file_out)
{
	char tmpfname[MAXPATHLEN];
	struct stat st;
	FILE *basis_file;
	FILE *sig_file = NULL;
	int r = -1;
	rs_result result;
	rs_stats_t stats;

	csync_debug(3, "Opening basis_file and sig_file for %s\n", filename);
	*sig_file_out = NULL;

	basis_file = fopen(prefixsubst(filename), "rb");
	if (!basis_file && errno == ENOENT) {
		csync_debug(3, "Basis file does not exist.\n");
		return 0;
	}
	if (!basis_file || fstat(fileno(basis_file), &st) || !S_ISREG(st.st_mode))
		goto out;

	sig_file = open_temp_file(tmpfname, prefixsubst(filename));
	if (!sig_file)
		goto out;
	if (unlink(tmpfname) < 0)
		goto out;

	csync_debug(3, "Running rs_sig_file() from librsync....\n");
/* see upstream
 * https://github.com/librsync/librsync/commit/152323729ac831727032daf50a10c1448b48f252
 * as reaction to SECURITY: CVE-2014-8242
 */
#ifdef RS_DEFAULT_STRONG_LEN
	result = rs_sig_file(basis_file, sig_file, RS_DEFAULT_BLOCK_LEN, RS_DEFAULT_STRONG_LEN, &stats);
#else
	/* For backward compatibility, for now hardcode RS_MD4_SIG_MAGIC.
	 * TODO: allow changing to RS_BLAKE2_SIG_MAGIC. */
	result = rs_sig_file(basis_file, sig_file, RS_DEFAULT_BLOCK_LEN, 0, RS_MD4_SIG_MAGIC, &stats);
#endif
	*sig_file_out = sig_file;
	sig_file = NULL;
	if (result != RS_DONE)
		csync_debug(0, "Internal error from rsync library!\n");
	else
		r = 0;
out:
	if (basis_file)
		fclose(basis_file);
	if (sig_file)
		fclose(sig_file);
	return r;
}

int csync_rs_check(const char *filename, int isreg)
{
	FILE *sig_file = 0;
	char buffer1[512], buffer2[512];
	int rc, chunk, found_diff = 0;
	int backup_errno;
	long size;
	long my_size = 0;

	csync_debug(3, "Csync2 / Librsync: csync_rs_check('%s', %d [%s])\n",
		    filename, isreg, isreg ? "regular file" : "non-regular file");

	csync_debug(3, "Reading signature size from peer....\n");
	if (!conn_gets(buffer1, 100) || sscanf(buffer1, "octet-stream %ld\n", &size) != 1)
		csync_fatal("Format-error while receiving data.\n");

	if (size < 0) {
		errno = EIO;
		goto io_error;
	}

	csync_debug(3, "Receiving %ld bytes ..\n", size);

	if (isreg) {
		if (csync_rs_sigfile(filename, &sig_file)) {
			if (!sig_file)
				goto io_error;
			goto error;
		}
		if (sig_file) {
			fflush(sig_file);
			my_size = ftell(sig_file);
			rewind(sig_file);
		}
	}

	if (size != my_size) {
		csync_debug(2, "Signature size differs: local=%d, peer=%d\n", my_size, size);
		found_diff = 1;
	}

	while (size > 0) {
		chunk = size > 512 ? 512 : size;
		rc = conn_read(buffer1, chunk);

		if (rc <= 0)
			csync_fatal("Read-error while receiving data.\n");
		chunk = rc;

		if (sig_file) {
			if (fread(buffer2, chunk, 1, sig_file) != 1) {
				csync_debug(2, "Found EOF in local sig file.\n");
				found_diff = 1;
			}
			if (memcmp(buffer1, buffer2, chunk)) {
				csync_debug(2, "Found diff in sig at -%d:-%d\n", size, size - chunk);
				found_diff = 1;
			}
		} /* else just drain */

		size -= chunk;
		csync_debug(3, "Got %d bytes, %ld bytes left ..\n", chunk, size);
	}

	csync_debug(3, "File has been checked successfully (%s).\n", found_diff ? "difference found" : "files are equal");
	if (sig_file)
		fclose(sig_file);
	return found_diff;

io_error:
	csync_debug(0, "I/O Error '%s' in rsync-check: %s\n", strerror(errno), prefixsubst(filename));
error:
	backup_errno = errno;

	/* drain response */
	while (size > 0) {
		chunk = size > 512 ? 512 : size;
		rc = conn_read(buffer1, chunk);
		if (rc <= 0)
			csync_fatal("Read-error while receiving data.\n");
		size -= rc;
	}

	if (sig_file)
		fclose(sig_file);
	errno = backup_errno;
	return -1;
}

void csync_rs_sig(const char *filename)
{
	FILE *sig_file;

	csync_debug(3, "Csync2 / Librsync: csync_rs_sig('%s')\n", filename);

	if (csync_rs_sigfile(filename, &sig_file)) {
		/* error */
		if (sig_file)
			csync_fatal("Got an error from librsync, too bad!\n");
		csync_debug(0, "I/O Error '%s' in rsync-sig: %s\n",
				strerror(errno), prefixsubst(filename));

		/* FIXME.
		 * Peer expected some sort of sig,
		 * we need to communicate an error instead. */
		conn_printf("octet-stream -1\n");
		return;
	}

	/* no error */
	csync_debug(3, "Sending sig_file to peer..\n");
	if (sig_file) {
		csync_send_file(sig_file);
		fclose(sig_file);
	} else {
		/* This is the signature for an "empty" file
		 * as returned by rs_sig_file(/dev/null).
		 * No point in re-calculating it over and over again. */
		conn_printf("octet-stream 12\n");
		conn_write("rs\0016\000\000\010\000\000\000\000\010", 12);
	}
	csync_debug(3, "Signature has been successfully sent.\n");
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

/* The "temporary" newfname typically has been created as "-rw------- root:root".
 * Before we rename it into place, "clone" the permissions and ownership of the
 * old file to avoid files already with the "correct name", but root owned and
 * unreadable by the applications, if even for a short time.
 * If the old file does not exist (or cannot be stat()ed), ignore.
 * If we cannot rename into place, but have to truncate/rewrite,
 * we keep the existing file anyways, so no ownership/permission change happens.
 * If permissions and/or ownership has also changed, in addition to
 * content, those will be explicitly synced "soon".
 * If there are errors while cloning the ownership or permissions, so what?
 * Ignore, it stays at root:root 0600, and won't be worse than before.
 *
 * Can we do better?
 * Until we change the ugly ad-hoc csync2 "protocol" to use "transaction" like
 * semantics, and know all of the changed content, plus ownership, permissions,
 * mtime, ACLs and other meta data as context information before starting to
 * act on it on the receiving side, I don't see how.
 */
static void clone_ownership_and_permissions(const char *newfname, const char *oldfname, struct stat *atomic_stats)
{
	struct stat sbuf;
	int uid, gid;

	if (atomic_stats) {
		sbuf = *atomic_stats;
	} else {
		if (stat(oldfname, &sbuf))
			return; /* At least we tried */
	}

	uid = csync_ignore_uid ? -1 : sbuf.st_uid;
	gid = csync_ignore_gid ? -1 : sbuf.st_gid;
	csync_debug(3, "Cloning ownership and permissions to tmp file: 0o%03o %d:%d %s [%s]\n",
			sbuf.st_mode, uid, gid, newfname, oldfname);
	if (chown(newfname, uid, gid))
		csync_debug(3, "Error '%s' for chown(%s,%d,%d) rsync-patch: %s\n",
				strerror(errno), newfname, uid, gid, oldfname);
	if (chmod(newfname, sbuf.st_mode))
		csync_debug(3, "Error '%s' for chmod(%s,0o%03o) rsync-patch: %s\n",
				strerror(errno), newfname, sbuf.st_mode, oldfname);

	/* FIXME also "clone" acls;
	 * as long as csync2 is no acl aware, there is no point, though */
}

int csync_rs_patch(const char *filename, struct stat *atomic_stats)
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
	basis_file = NULL;

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
		goto copy;
	}
#endif

	clone_ownership_and_permissions(newfname, prefixsubst(filename), atomic_stats);

	// Set modification time
	fflush(new_file);
	if (atomic_stats) {
		struct timespec tsp[2];
		tsp[0].tv_sec = tsp[1].tv_sec = atomic_stats->st_mtim.tv_sec;
		tsp[0].tv_nsec = tsp[1].tv_nsec =  atomic_stats->st_mtim.tv_nsec;
		if(utimensat(0, newfname, tsp, 0))
			csync_debug(1, "Could not change the modification date\n");

	}

	if (rename(newfname, prefixsubst(filename))) {
		char buffer[512];
		int rc;

		if (errno != EXDEV) {
			errstr="renaming tmp file to to be patched file";
			goto io_error;
		}
#ifdef __CYGWIN__
copy:
#endif
		csync_debug(1, "rename not possible! Will truncate and copy instead.\n");
		basis_file = fopen(prefixsubst(filename), "wb");
		if ( !basis_file ) {
			errstr="opening data file for writing";
			goto io_error;
		}

		/* FIXME
		 * Can easily lead to partially transfered files on the receiving side!
		 * Think truncate, then connection loss.
		 * Or any other failure scenario.
		 * Need better error checks!
		 */
		rewind(new_file);
		while ( (rc = fread(buffer, 1, 512, new_file)) > 0
			&& fwrite(buffer, 1, rc, basis_file) == rc )
			;
		/* at least retain the temp file, if something went wrong. */
		if (ferror(new_file) || ferror(basis_file)) {
			csync_debug(0, "ERROR while copying temp file '%s' to basis file '%s'; "
					"basis file may be corrupted; temp file has been retained.\n",
					newfname, prefixsubst(filename));
			goto error;
		}
		unlink(newfname);
		fclose(basis_file);
		basis_file = NULL;
	}

	csync_debug(3, "File has been patched successfully.\n");
	fclose(delta_file);
	fclose(new_file);

	return 0;

io_error:
	backup_errno = errno;
	csync_debug(0, "I/O Error '%s' while %s in rsync-patch: %s\n",
			strerror(errno), errstr, prefixsubst(filename));
	errno = backup_errno;
error:;
	backup_errno = errno;
	if ( delta_file ) fclose(delta_file);
	if ( basis_file ) fclose(basis_file);
	if ( new_file )   fclose(new_file);
	errno = backup_errno;
	return -1;
}

