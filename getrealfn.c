#include "csync2.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

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
		char *t2, *t1 = get_current_dir_name();
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
				}
			}
			*(target++) = *source;
		}
		*target = 0;
	}

	/* this case is trivial */
	if ( !strcmp(tempfn, "/") )
		goto return_filename;

	/* find the last stat-able directory element */
	while ( stat(tempfn, &st) || !S_ISDIR(st.st_mode) ) {
		char *tmp = st_mark;
		st_mark = strrchr(tempfn, '/');
		if ( tmp ) *tmp = '/';
		assert( st_mark != 0 );
		if ( st_mark == tempfn ) goto return_filename;
		*st_mark = 0;
	}

	/* ok - this might be ugly, but who cares .. */
	{
		char *oldpwd = get_current_dir_name();
		if ( !chdir(tempfn) ) {
			char *t2, *t1 = get_current_dir_name();
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
	if (ret) free(ret);
	return (ret=tempfn);
}

#if 0
/* debugging main function to debug this code stand-alone */
int main(int argc, char ** argv)
{
	for (int i=1; i<argc; i++)
		printf("%s -> %s\n", argv[i], getrealfn(argv[i]));
	return 0;
}
#endif

