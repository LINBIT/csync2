/*
 *  csync2 - cluster synchronization tool, 2nd generation
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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

static char *my_get_current_dir_name()
{
#if defined __CYGWIN__ || defined __FreeBSD__ || defined __OpenBSD__ || defined __NetBSD__
	char *r = malloc(1024);
	if (!getcwd(r, 1024))
		strcpy(r, "/__PATH_TO_LONG__");
	return r;
#else
	return get_current_dir_name();
#endif
}

/*
 * glibc's realpath() is broken - so don't use it!
 */
char *getrealfn(const char *filename)
{
	static char *ret = 0;
	char *st_mark = 0;
	struct stat st;
	char *tempfn;

	/* create working copy of filename */
	tempfn = strdup(filename);

	/* make the path absolute */
	if ( *tempfn != '/' ) {
		char *t2, *t1 = my_get_current_dir_name();
		asprintf(&t2, "%s/%s", t1, tempfn);
		free(t1); free(tempfn); tempfn = t2;
	}

	/* remove leading slashes from tempfn */
	{
		char *tmp = tempfn + strlen(tempfn) - 1;
		while (tmp > tempfn && *tmp == '/') *(tmp--)=0;
	}

	/* get rid of the .. and // entries */
	{
		char *source = tempfn, *target = tempfn;
		for (; *source; source++) {
			if ( *source == '/' ) {
				if ( *(source+1) == '/' ) continue;
				if ( !strncmp(source, "/../", 4) ||
						!strcmp(source, "/..") ) {
					while (1) {
					    if ( target == tempfn ) break;
					    if ( *(--target) == '/' ) break;
					}
					source += 2;
					continue;
				} else
				if ( !strncmp(source, "/./", 3) ) {
					source += 2;
				}
			}
			*(target++) = *source;
		}
		*target = 0;
	}

	/* this case is trivial */
	if ( !strcmp(tempfn, "/") )
		goto return_filename;

	/* find the last stat-able directory element, but don't use the */
	/* leaf-node because we do not want to resolve a symlink there. */
	do {
		char *tmp = st_mark;
		st_mark = strrchr(tempfn, '/');
		if ( tmp ) *tmp = '/';
		assert( st_mark != 0 );
		if ( st_mark == tempfn ) goto return_filename;
		*st_mark = 0;
	} while ( stat(tempfn, &st) || !S_ISDIR(st.st_mode) );

	/* ok - this might be ugly, but who cares .. */
	{
		char *oldpwd = my_get_current_dir_name();
		if ( !chdir(tempfn) ) {
			char *t2, *t1 = my_get_current_dir_name();
			if ( st_mark ) {
				asprintf(&t2, "%s/%s", t1, st_mark+1);
				free(tempfn); free(t1); tempfn = t2;
			} else {
				free(tempfn); tempfn = t1;
			}
			chdir(oldpwd);
		} else
			if ( st_mark ) *st_mark = '/';
	}

return_filename:
	/* remove a possible "/." from the end */
	{
		int len = strlen(tempfn);
		if ( len >= 2 && !strcmp(tempfn+len-2, "/.") ) {
			if (len == 2) len++;
			*(tempfn+len-2) = 0;
		}
	}

	if (ret) free(ret);
	return (ret=tempfn);
}

#ifdef DEBUG_GETREALFN_MAIN
/* debugging main function to debug this code stand-alone */
int main(int argc, char ** argv)
{
	int i;
	for (i=1; i<argc; i++)
		printf("%s -> %s\n", argv[i], getrealfn(argv[i]));
	return 0;
}
#endif

