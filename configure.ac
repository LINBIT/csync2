# Process this file with autoconf to produce a configure script.
AC_INIT(csync2, SNAPSHOT, clifford@clifford.at)
AC_CONFIG_SRCDIR(csync2.c)
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE

# Use /etc and /var instead of $prefix/...
test "$localstatedir" = '${prefix}/var' && localstatedir=/var
test "$sysconfdir" = '${prefix}/etc' && sysconfdir=/etc

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_YACC
AM_PROG_LEX

# Checks for libraries.
AC_CHECK_LIB([rsync], [rs_sig_file], , [AC_MSG_ERROR(librsync is required)])
AC_CHECK_LIB([sqlite], [sqlite_exec], , [AC_MSG_ERROR(libsqlite is required)])
AC_CHECK_LIB([ssl], [SSL_new], , [AC_MSG_ERROR(libssl is required)])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
