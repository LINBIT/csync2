# Process this file with autoconf to produce a configure script.
AC_INIT(csync2, SNAPSHOT, clifford@clifford.at)
AM_INIT_AUTOMAKE

AC_CONFIG_SRCDIR(csync2.c)
AM_CONFIG_HEADER(config.h)

# Use /etc and /var instead of $prefix/...
test "$localstatedir" = '${prefix}/var' && localstatedir=/var
test "$sysconfdir" = '${prefix}/etc' && sysconfdir=/etc

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_YACC
AM_PROG_LEX

# Check for librsync.
AC_ARG_WITH([librsync-source],
	AS_HELP_STRING([--with-librsync-source=source-tar-file],
		[build this librsync and link statically against it (hack! hack!)]),
	AC_SUBST([librsync_source_file], $withval),
	AC_CHECK_LIB([rsync], [rs_sig_file], , [AC_MSG_ERROR(librsync is required)])
)
AM_CONDITIONAL([PRIVATE_LIBRSYNC], [test -n "$librsync_source_file"])

# Check for libsqlite.
AC_ARG_WITH([libsqlite-source],
	AS_HELP_STRING([--with-libsqlite-source=source-tar-file],
		[build this libsqlite and link statically against it (hack! hack!)]),
	AC_SUBST([libsqlite_source_file], $withval),
	AC_CHECK_LIB([sqlite], [sqlite_exec], , [AC_MSG_ERROR(libsqlite is required)])
)
AM_CONDITIONAL([PRIVATE_LIBSQLITE], [test -n "$libsqlite_source_file"])

# Check for libssl.
AC_CHECK_LIB([ssl], [SSL_new], , [AC_MSG_ERROR(libssl is required)])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
