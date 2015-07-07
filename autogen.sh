#!/bin/bash -x
#
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

aclocal
autoheader
automake --add-missing --copy
autoconf

if [ "$1" = clean ]; then
	./configure && make distclean
	rm -rf librsync[.-]* libsqlite.* sqlite-*
	rm -rf configure Makefile.in depcomp stamp-h.in
	rm -rf mkinstalldirs config.h.in autom4te.cache
	rm -rf missing aclocal.m4 install-sh *~
	rm -rf config.guess config.sub
	rm -rf cygwin/librsync-0.9.7.tar.gz
	rm -rf cygwin/sqlite-2.8.16.tar.gz
else
	./configure  --prefix=/usr --localstatedir=/var --sysconfdir=/etc

	echo ""
	echo "Configured as"
	echo "./configure  --prefix=/usr --localstatedir=/var --sysconfdir=/etc"
	echo ""
	echo "reconfigure, if you want it different"
fi

