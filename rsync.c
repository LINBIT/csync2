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
#include <librsync.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

static FILE *paranoid_tmpfile()
{
	FILE *f;

	if ( access(P_tmpdir, R_OK|W_OK|X_OK) < 0 )
		csync_fatal("Temp directory '%s' does not exist!\n", P_tmpdir);

	f = tmpfile();
	if ( !f ) csync_debug(0, "ERROR: tmpfile() didn't return a valid file handle!\n");

	return f;
}

void csync_send_file(FILE * in)
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

void csync_send_error()
{
	conn_printf("ERROR\n");
}

int csync_recv_file(FILE * out)
{
	char buffer[512];
	int rc, chunk;
	long size;

	if ( !conn_gets(buffer, 100) || sscanf(buffer, "octet-stream %ld\n", &size) != 1 ) {
		if (!strcmp(buffer, "ERROR\n")) return -1;
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
	FILE *basis_file, *sig_file;
	char buffer1[512], buffer2[512];
	int rc, chunk, found_diff = 0;
	rs_stats_t stats;
	rs_result result;
	long size;

	csync_debug(3, "Csync2 / Librsync: csync_rs_check('%s', %d [%s])\n",
		filename, isreg, isreg ? "regular file" : "non-regular file");

	csync_debug(3, "Opening basis_file and sig_file..\n");

	sig_file = paranoid_tmpfile();
	if ( !sig_file ) goto io_error;

	basis_file = fopen(filename, "r");
	if ( !basis_file ) basis_file = paranoid_tmpfile();
	if ( !basis_file ) goto io_error;

	if ( isreg ) {
		csync_debug(3, "Running rs_sig_file() from librsync....\n");
		result = rs_sig_file(basis_file, sig_file,
				RS_DEFAULT_BLOCK_LEN, RS_DEFAULT_STRONG_LEN, &stats);
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
		if ( !conn_gets(line, 100) || sscanf(line, "octet-stream %ld\n", &size) != 1 )
			csync_fatal("Format-error while receiving data.\n");
	}

	fflush(sig_file);
	if ( size != ftell(sig_file) ) {
		csync_debug(2, "Signature size differs: local=%d, peer=%d\n",
				ftell(sig_file), size);
		found_diff = 1;
	}
	rewind(sig_file);

	csync_debug(3, "Receiving %ld bytes ..\n", size);

	while ( size > 0 ) {
		chunk = size > 512 ? 512 : size;
		rc = conn_read(buffer1, chunk);

		if ( rc <= 0 )
			csync_fatal("Read-error while receiving data.\n");
		chunk = rc;

		if ( fread(buffer2, chunk, 1, sig_file) != 1 ) {
			csync_debug(2, "Found EOF in local sig file.\n");
			found_diff = 1;
		}
		if ( memcmp(buffer1, buffer2, chunk) ) {
			csync_debug(2, "Found diff in sig at -%d:-%d\n",
					size, size-chunk);
			found_diff = 1;
		}

		size -= chunk;
		csync_debug(3, "Got %d bytes, %ld bytes left ..\n",
				chunk, size);
	}

	csync_debug(3, "File has been checked successfully (%s).\n",
		found_diff ? "difference found" : "files are equal");
	fclose(sig_file);
	return found_diff;

io_error:
	csync_debug(0, "I/O Error '%s' in rsync-check: %s\n",
			strerror(errno), filename);

error:
	if ( basis_file ) fclose(basis_file);
	if ( sig_file )   fclose(sig_file);

	return -1;
}

void csync_rs_sig(const char *filename)
{
	FILE *basis_file, *sig_file;
	rs_stats_t stats;
	rs_result result;

	csync_debug(3, "Csync2 / Librsync: csync_rs_sig('%s')\n", filename);

	csync_debug(3, "Opening basis_file and sig_file..\n");
	sig_file = paranoid_tmpfile();
	basis_file = fopen(filename, "r");
	if ( !basis_file ) basis_file = fopen("/dev/null", "r");

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
}

int csync_rs_delta(const char *filename)
{
	FILE *sig_file, *new_file, *delta_file;
	rs_result result;
	rs_signature_t *sumset;
	rs_stats_t stats;

	csync_debug(3, "Csync2 / Librsync: csync_rs_delta('%s')\n", filename);

	csync_debug(3, "Receiving sig_file from peer..\n");
	sig_file = paranoid_tmpfile();
	if ( csync_recv_file(sig_file) ) {
		fclose(sig_file);
		return -1;
	}
	result = rs_loadsig_file(sig_file, &sumset, &stats);
	if (result != RS_DONE)
		csync_fatal("Got an error from librsync, too bad!\n");
	fclose(sig_file);

	csync_debug(3, "Opening new_file and delta_file..\n");
	new_file = fopen(filename, "r");
	if ( !new_file ) {
		csync_debug(0, "I/O Error '%s' while %s in rsync-delta: %s\n",
				strerror(errno), "opening data file for reading", filename);
		csync_send_error();
		fclose(new_file);
		return -1;
	}
	delta_file = paranoid_tmpfile();

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
}

int csync_rs_patch(const char *filename)
{
	FILE *basis_file = 0, *delta_file = 0, *new_file = 0;
	rs_stats_t stats;
	rs_result result;
	char buffer[512];
	char *errstr = "?";
	int rc;

	csync_debug(3, "Csync2 / Librsync: csync_rs_patch('%s')\n", filename);

	csync_debug(3, "Receiving delta_file from peer..\n");
	delta_file = paranoid_tmpfile();
	if ( !delta_file ) { errstr="creating delta temp file"; goto io_error; }
	if ( csync_recv_file(delta_file) ) goto error;

	csync_debug(3, "Opening to be patched file on local host..\n");
	basis_file = fopen(filename, "r");
	if ( !basis_file ) basis_file = paranoid_tmpfile();
	if ( !basis_file ) { errstr="opening data file for reading"; goto io_error; }

	csync_debug(3, "Opening temp file for new data on local host..\n");
	new_file = paranoid_tmpfile();
	if ( !new_file ) { errstr="creating new data temp file"; goto io_error; }

	csync_debug(3, "Running rs_patch_file() from librsync..\n");
	result = rs_patch_file(basis_file, delta_file, new_file, &stats);
	if (result != RS_DONE) {
		csync_debug(0, "Internal error from rsync library!\n");
		goto error;
	}

	csync_debug(3, "Copying new data to local file..\n");
	fclose(basis_file);
	rewind(new_file);

	unlink(filename);
	basis_file = fopen(filename, "w");
	if ( !basis_file ) { errstr="opening data file for writing"; goto io_error; }

	while ( (rc = fread(buffer, 1, 512, new_file)) > 0 )
		fwrite(buffer, rc, 1, basis_file);

	csync_debug(3, "File has been patched successfully.\n");
	fclose(basis_file);
	fclose(delta_file);
	fclose(new_file);

	return 0;

io_error:
	csync_debug(0, "I/O Error '%s' while %s in rsync-patch: %s\n",
			strerror(errno), errstr, filename);

error:
	if ( delta_file ) fclose(delta_file);
	if ( basis_file ) fclose(basis_file);
	if ( new_file )   fclose(new_file);

	return -1;
}

