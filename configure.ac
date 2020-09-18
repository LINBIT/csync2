# csync2 - cluster synchronization tool, 2nd generation
# Copyright (C) 2004 - 2015 LINBIT Information Technologies GmbH
# http://www.linbit.com; see also AUTHORS
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

# Process this file with autoconf to produce a configure script.
AC_INIT(csync2, 2.1-0rc1, csync2@lists.linbit.com)
AM_INIT_AUTOMAKE

AC_CONFIG_SRCDIR(csync2.c)
AM_CONFIG_HEADER(config.h)

# Use /etc and /var instead of $prefix/...
# test "$localstatedir" = '${prefix}/var' && localstatedir=/var
# test "$sysconfdir" = '${prefix}/etc' && sysconfdir=/etc

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_YACC
AM_PROG_LEX

dnl inspired by rsync's configure.ac
AC_CHECK_FUNCS(fchmod setmode open64 mkstemp64 strlcpy)
AC_CACHE_CHECK([for secure mkstemp],csync_cv_HAVE_SECURE_MKSTEMP,[
AC_TRY_RUN([#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
main() {
  struct stat st;
  char tpl[20]="/tmp/test.XXXXXX";
  int fd = mkstemp(tpl);
  if (fd == -1) exit(1);
  unlink(tpl);
  if (fstat(fd, &st) != 0) exit(1);
  if ((st.st_mode & 0777) != 0600) exit(1);
  exit(0);
}],
csync_cv_HAVE_SECURE_MKSTEMP=yes,
csync_cv_HAVE_SECURE_MKSTEMP=no,
csync_cv_HAVE_SECURE_MKSTEMP=cross)])
if test x"$csync_cv_HAVE_SECURE_MKSTEMP" = x"yes"; then
    case $host_os in
    hpux*)
	dnl HP-UX has a broken mkstemp() implementation they refuse to fix,
	dnl so we noisily skip using it.  See HP change request JAGaf34426
	dnl for details. (sbonds)
	AC_MSG_WARN(Skipping broken HP-UX mkstemp() -- using mktemp() instead)
	;;
    *)
	AC_DEFINE(HAVE_SECURE_MKSTEMP, 1, [Define to 1 if mkstemp() is available and works right])
	;;
    esac
fi

# check for large file support
AC_SYS_LARGEFILE

# Check for librsync.
AC_ARG_WITH([librsync-source],
	AS_HELP_STRING([--with-librsync-source=source-tar-file],
		[build this librsync and link statically against it (hack! hack!)]),
	AC_SUBST([librsync_source_file], $withval),
	AC_CHECK_LIB([rsync], [rs_sig_file], , [AC_MSG_ERROR(librsync is required)])
)
AM_CONDITIONAL([PRIVATE_LIBRSYNC], [test -n "$librsync_source_file"])

AC_ARG_ENABLE([sqlite],
	[AC_HELP_STRING([--enable-sqlite],
		[enable/disable sqlite 2 support (default is disabled)])],
	[], [ enable_sqlite=no ])

if test "$enable_sqlite" == yes
then
        AC_CHECK_HEADERS([sqlite.h], , [AC_MSG_ERROR([[SQLite header not found; install libsqlite-dev and dependencies for SQLite 2 support]])])

	AC_DEFINE([HAVE_SQLITE], 1, [Define if sqlite 2 support is wanted])
fi

AC_ARG_ENABLE([sqlite3],
	[AC_HELP_STRING([--disable-sqlite3],
		[enable/disable sqlite3 support (default is enabled)])],
	[], [ enable_sqlite3=yes ])

if test "$enable_sqlite3" == yes
then
        AC_CHECK_HEADERS([sqlite3.h], , [AC_MSG_ERROR([[SQLite header not found; install libsqlite3-dev and dependencies for SQLite 3 support]])])

	AC_DEFINE([HAVE_SQLITE3], 1, [Define if sqlite3 support is wanted])
fi

AC_ARG_ENABLE([gnutls],
	[AS_HELP_STRING([--disable-gnutls],[enable/disable GNU TLS support (default is enabled)])],
	[], [ enable_gnutls=yes ])

PKG_PROG_PKG_CONFIG
if test "$enable_gnutls" != no
then
   PKG_CHECK_MODULES([LIBGNUTLS], [gnutls >= 2.6.0], [
      AC_DEFINE([HAVE_LIBGNUTLS], 1, [Define to 1 when using GNU TLS library])
   ])
fi

AC_ARG_ENABLE([mysql],
	[AC_HELP_STRING([--enable-mysql],
		[enable/disable MySQL support (default is disabled)])],
	[], [ enable_mysql=no ])

AC_ARG_ENABLE([postgres],
	[AC_HELP_STRING([--enable-postgres],
		[enable/disable Postgres support (default is disabled)])],
	[], [ enable_postgres=no ])

if test "$enable_mysql" == yes
then
	# Check for mysql.
	CFLAGS="$CFLAGS `pkg-config --cflags mysqlclient`"

	# Check MySQL development header
	AC_CHECK_HEADERS([mysql.h], , [AC_MSG_ERROR([[mysql header not found; install mysql-devel and dependencies for MySQL Support]])])

	AC_DEFINE([HAVE_MYSQL], 1, [Define if mysql support is wanted])

	# [] quotes, or autofoo will strip the character class [] in sed, breaking the regex.
	[LIBMYSQLCLIENT_SO=$( readlink $(pkg-config --variable=libdir mysqlclient)/libmysqlclient.so | sed -e 's,^.*/,,;s/\(\.so\.[0-9]*\)\..*$/\1/')]
	test -n "$LIBMYSQLCLIENT_SO" || AC_MSG_ERROR([Could not determine library name to be used in dlopen for mysql support])
fi
AC_DEFINE_UNQUOTED([LIBMYSQLCLIENT_SO], ["$LIBMYSQLCLIENT_SO"], [library name to be used in dlopen for mysql support])

if test "$enable_postgres" == yes
then
	CFLAGS="$CFLAGS `pkg-config --cflags libpq`"
        AC_CHECK_HEADERS([libpq-fe.h], , [AC_MSG_ERROR([[postgres header not found; install libpq-dev and dependencies for Postgres support]])])

	AC_DEFINE([HAVE_POSTGRES], 1, [Define if postgres support is wanted])

	# Hmpf. libdir not included in libpq.pc; use pg_config
	# LIBPQ_SO=$( readlink $(pkg-config --variable=libdir libpq)/libpq.so | sed -e 's,^.*/,,;s/\(\.so\.[0-9]*\)\..*$/\1/')
	# [] quotes, or autofoo will strip the character class [] in sed, breaking the regex.
	[LIBPQ_SO=$( readlink $(pg_config --libdir)/libpq.so | sed -e 's,^.*/,,;s/\(\.so\.[0-9]*\)\..*$/\1/')]
	test -n "$LIBPQ_SO" || AC_MSG_ERROR([Could not determine library name to be used in dlopen for postgres support])
fi
AC_DEFINE_UNQUOTED([LIBPQ_SO], ["$LIBPQ_SO"], [library name to be used in dlopen for postgres support])

# at least one db backend must be configured.

if test "$enable_postgres" != yes && test "$enable_mysql" != yes &&
   test "$enable_sqlite3" != yes && test "$enable_sqlite" != yes
then
	AC_MSG_ERROR([No database backend configured. Please enable either sqlite, sqlite3, mysql or postgres.])
fi

AC_ARG_ENABLE([systemd],
	AS_HELP_STRING([--enable-systemd], [enable systemd support]),
	[use_systemd=$enableval], [use_systemd=no])
AM_CONDITIONAL(USE_SYSTEMD, test "x$use_systemd" != "xno")
AC_SUBST(USE_SYSTEMD)

AC_ARG_WITH([systemd-unitdir],
	AS_HELP_STRING([--with-systemd-unitdir=PATH],
		[specify exact directory for systemd service files, defaults to PREFIX/lib/systemd/system/]),
	[SYSTEMD_UNITDIR=$withval], [SYSTEMD_UNITDIR="${prefix}/lib/systemd/system"])
AC_SUBST(SYSTEMD_UNITDIR)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
