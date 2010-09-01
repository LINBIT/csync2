# csync2 - cluster synchronization tool, 2nd generation
# LINBIT Information Technologies GmbH <http://www.linbit.com>
# Copyright (C) 2004, 2005  Clifford Wolf <clifford@clifford.at>
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
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

# Process this file with autoconf to produce a configure script.
AC_INIT(csync2, SNAPSHOT, csync2@lists.linbit.com)
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

AC_ARG_ENABLE([sqlite3],
	[AC_HELP_STRING([--disable-sqlite3],
		[enable/disable sqlite3 support (default is enabled)])],
	[], [ enable_sqlite3=yes ])

if test "$enable_sqlite3" != no
then
# Check for libsqlite3.
AC_ARG_WITH([libsqlite-source],
	AS_HELP_STRING([--with-libsqlite-source=source-tar-file],
		[build this libsqlite and link statically against it (hack! hack!)]),
	AC_SUBST([libsqlite_source_file], $withval),
	AC_CHECK_LIB([sqlite3], [sqlite3_exec], , [AC_MSG_ERROR(libsqlite3 is required)])
)
AM_CONDITIONAL([PRIVATE_LIBSQLITE], [test -n "$libsqlite_source_file"])
else
# Check for libsqlite.
AC_ARG_WITH([libsqlite-source],
	AS_HELP_STRING([--with-libsqlite-source=source-tar-file],
		[build this libsqlite and link statically against it (hack! hack!)]),
	AC_SUBST([libsqlite_source_file], $withval),
	AC_CHECK_LIB([sqlite], [sqlite_exec], , [AC_MSG_ERROR(libsqlite is required)])
)
AM_CONDITIONAL([PRIVATE_LIBSQLITE], [test -n "$libsqlite_source_file"])
fi

AC_ARG_ENABLE([gnutls],
	[AS_HELP_STRING([--disable-gnutls],[enable/disable GNU TLS support (default is enabled)])],
	[], [ enable_gnutls=yes ])

if test "$enable_gnutls" != no
then
	# Check for gnuTLS.
	##AM_PATH_LIBGNUTLS(1.0.0, , [ AC_MSG_ERROR([[gnutls not found; install gnutls, gnutls-openssl and libtasn1 packages for your system or run configure with --disable-gnutls]]) ])
   PKG_PROG_PKG_CONFIG
   PKG_CHECK_MODULES([LIBGNUTLS], [gnutls >= 1.0.0], [
      AC_DEFINE([USE_GNUTLS], 1, [Define to 1 when using GNU TLS library])
      gnutls_version_code=`$PKG_CONFIG --modversion gnutls | $AWK -F. '{ printf "0x%02X%02X%02X\n",$1,$2,$3 }'`
      AC_DEFINE_UNQUOTED([LFTP_LIBGNUTLS_VERSION_CODE], $gnutls_version_code, [Define to libgnutls version, e.g. 0x010203 for 1.2.3])
   ])

	## This is a bloody hack for fedora core
	#CFLAGS="$CFLAGS $LIBGNUTLS_CFLAGS"
	#LIBS="$LIBS $LIBGNUTLS_LIBS -ltasn1"
fi
AM_CONDITIONAL([HAVE_LIBGNUTLS], [test "$enable_gnutls" != no ])

AC_ARG_ENABLE([mysql],
	[AC_HELP_STRING([--enable-mysql],
		[enable/disable MySQL support (default is disabled)])],
	[], [ enable_mysql=no ])

if test "$enable_mysql" == yes
then
	# Check for mysql.
	# This is a bloody hack for fedora core
	CFLAGS="$CFLAGS `mysql_config --cflags`"	
	LIBS="$LIBS `mysql_config --libs`"

	# Check MySQL compatibility lib.
	AC_CHECK_LIB([mysqlclient], [mysql_get_client_info], , [AC_MSG_ERROR([[mysql not found; install mysql-devel and dependencies for MySQL Support]])])

fi
AM_CONDITIONAL([HAVE_LIBMYSQL], [test "$enable_mysql" != no ])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
