/*
 *  csync2 - cluster syncronisation tool, 2nd generation
 *  LINBIT Information Technologies <http://www.linbit.com>
 *  Copyright (C) 2004  Clifford Wolf <clifford@clifford.at>
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

void csync_send_file(FILE * in, FILE * out)
{
	char buffer[512];
	int rc, chunk;
	long size;
	
	fflush(in);
	size = ftell(in);
	rewind(in);

	fprintf(out, "octet-stream %ld\n", size);

	while ( size > 0 ) {
		chunk = size > 512 ? 512 : size;
		rc = fread(buffer, 1, chunk, in);

		if ( rc <= 0 )
			csync_fatal("Read-error while sending data.\n");
		chunk = rc;

		rc = fwrite(buffer, chunk, 1, out);
		if ( rc != 1 )
			csync_fatal("Write-error while sending data.\n");

		size -= chunk;
	}

	fflush(out);
}

void csync_recv_file(FILE * in, FILE * out)
{
	char buffer[512];
	int rc, chunk;
	long size;

	if ( fscanf(in, "octet-stream %ld\n", &size) != 1 )
		csync_fatal("Format-error while receiving data.\n");

	csync_debug(3, "Receiving %ld bytes ..\n", size);

	while ( size > 0 ) {
		chunk = size > 512 ? 512 : size;
		rc = fread(buffer, 1, chunk, in);

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
}

/* FIXME: This function does currently not compare the signatures!
 * Instead it just skips the input and returns the the both streams
 * are identical.
 */
int csync_rs_check(const char * filename, FILE * in_sig)
{
	char buffer[512];
	int rc, chunk;
	long size;

	if ( fscanf(in_sig, "octet-stream %ld\n", &size) != 1 )
		csync_fatal("Format-error while receiving data.\n");

	csync_debug(3, "Receiving %ld bytes ..\n", size);

	while ( size > 0 ) {
		chunk = size > 512 ? 512 : size;
		rc = fread(buffer, 1, chunk, in_sig);

		if ( rc <= 0 )
			csync_fatal("Read-error while receiving data.\n");
		chunk = rc;

		size -= chunk;
		csync_debug(3, "Got %d bytes, %ld bytes left ..\n",
				chunk, size);
	}

	return 0;
}

void csync_rs_sig(const char * filename, FILE * out_sig)
{
	FILE *basis_file, *sig_file;
	rs_stats_t stats;
	rs_result result;

	sig_file = tmpfile();
	basis_file = fopen(filename, "r");
	if ( !basis_file ) basis_file = fopen("/dev/null", "r");

	result = rs_sig_file(basis_file, sig_file,
			RS_DEFAULT_BLOCK_LEN, RS_DEFAULT_STRONG_LEN, &stats);
	if (result != RS_DONE)
		csync_fatal("Got an error from librsync, too bad!\n");

	csync_send_file(sig_file, out_sig);

	fclose(basis_file);
	fclose(sig_file);
}

void csync_rs_delta(const char * filename, FILE * in_sig, FILE * out_delta)
{
	FILE *sig_file, *new_file, *delta_file;
	rs_result result;
	rs_signature_t *sumset;
	rs_stats_t stats;

	sig_file = tmpfile();
	csync_recv_file(in_sig, sig_file);
	result = rs_loadsig_file(sig_file, &sumset, &stats);
	if (result != RS_DONE)
		csync_fatal("Got an error from librsync, too bad!\n");
	fclose(sig_file);

	new_file = fopen(filename, "r");
	delta_file = tmpfile();

	result = rs_build_hash_table(sumset);
	if (result != RS_DONE)
		csync_fatal("Got an error from librsync, too bad!\n");

	result = rs_delta_file(sumset, new_file, delta_file, &stats);
	if (result != RS_DONE)
		csync_fatal("Got an error from librsync, too bad!\n");

	csync_send_file(delta_file, out_delta);

	rs_free_sumset(sumset);
	fclose(delta_file);
	fclose(new_file);
}

void csync_rs_patch(const char * filename, FILE * in_delta)
{
	FILE *basis_file, *delta_file, *new_file;
	rs_stats_t stats;
	rs_result result;

	delta_file = tmpfile();
	csync_recv_file(in_delta, delta_file);

	basis_file = fopen(filename, "r");
	if ( basis_file ) unlink(filename);
	else basis_file = fopen("/dev/null", "r");

	new_file = fopen(filename, "w");

	result = rs_patch_file(basis_file, delta_file, new_file, &stats);
	if (result != RS_DONE)
		csync_fatal("Got an error from librsync, too bad!\n");

	fclose(delta_file);
	fclose(basis_file);
	fclose(new_file);
}

