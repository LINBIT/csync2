#!/bin/bash -ex
#
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

TRGDIR=/cygdrive/c/csync2

if ! [ -f sqlite-2.8.16.tar.gz ]; then
	wget http://www.sqlite.org/sqlite-2.8.16.tar.gz -O sqlite-2.8.16.tar.gz
fi

if ! [ -f librsync-0.9.7.tar.gz ]; then
	wget http://mesh.dl.sourceforge.net/sourceforge/librsync/librsync-0.9.7.tar.gz -O librsync-0.9.7.tar.gz
fi

cd ..
mkdir -p $TRGDIR
rm -f $TRGDIR/*.exe $TRGDIR/*.dll

if ! [ -f config.h ]; then
	./configure \
		--with-librsync-source=cygwin/librsync-0.9.7.tar.gz \
		--with-libsqlite-source=cygwin/sqlite-2.8.16.tar.gz \
		--disable-gnutls --sysconfdir=$TRGDIR
fi

make private_librsync
make private_libsqlite
make CFLAGS='-DREAL_DBDIR=\".\"'

ignore_dlls="KERNEL32.dll|USER32.dll|GDI32.dll|mscoree.dll"
copy_dlls() {
	for dll in $( strings "$@" | egrep '^[^ ]+\.dll$' | sort -u; )
	do
		if echo "$dll" | egrep -qv "^($ignore_dlls)\$"
		then
			cp -v /bin/$dll $TRGDIR/$dll
			ignore_dlls="$ignore_dlls|$dll"
			copy_dlls $TRGDIR/$dll
		fi
	done
}

cp -v csync2.exe $TRGDIR/csync2.exe
cp -v sqlite-2.8.16/sqlite.exe $TRGDIR/sqlite.exe
cp -v /bin/killall.exe /bin/cp.exe /bin/ls.exe /bin/wc.exe $TRGDIR/
cp -v /bin/find.exe /bin/xargs.exe /bin/rsync.exe $TRGDIR/
cp -v /bin/grep.exe /bin/gawk.exe /bin/wget.exe $TRGDIR/
cp -v /bin/rxvt.exe /bin/unzip.exe /bin/libW11.dll $TRGDIR/
cp -v /bin/diff.exe /bin/date.exe /bin/tail.exe $TRGDIR/
cp -v /bin/head.exe /bin/sleep.exe /bin/rm.exe $TRGDIR/
cp -v /bin/bash.exe $TRGDIR/sh.exe

copy_dlls $TRGDIR/*.exe $TRGDIR/*.dll

cd cygwin
PATH="$PATH:/cygdrive/c/WINNT/Microsoft.NET/Framework/v1.0.3705"
csc /nologo cs2hintd_fseh.cs

gcc -Wall cs2monitor.c -o cs2monitor.exe -DTRGDIR="\"$TRGDIR"\" -{I,L}../sqlite-2.8.16 -lprivatesqlite
gcc -Wall ../urlencode.o cs2hintd.c -o cs2hintd.exe -{I,L}../sqlite-2.8.16 -lprivatesqlite

cp -v readme_pkg.txt $TRGDIR/README.txt
cp -v ../README $TRGDIR/README-csync2.txt
cp -v cs2hintd_fseh.exe cs2hintd.exe cs2monitor.exe $TRGDIR/

cd $( dirname $TRGDIR/ )
rm -f $( basename $TRGDIR ).zip
zip -r $( basename $TRGDIR ).zip $( basename $TRGDIR ) \
	-i '*.txt' '*.dll' '*.exe'

echo "DONE."

