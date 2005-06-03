#!/bin/bash

TRGDIR=/cygdrive/c/csync2

if ! [ -f sqlite-2.8.16.tar.gz ]; then
	wget http://www.sqlite.org/sqlite-2.8.16.tar.gz -O sqlite-2.8.16.tar.gz
fi

if ! [ -f librsync-0.9.7.tar.gz ]; then
	wget http://mesh.dl.sourceforge.net/sourceforge/librsync/librsync-0.9.7.tar.gz -O librsync-0.9.7.tar.gz
fi

cd ..
mkdir -p $TRGDIR

./configure \
	--with-librsync-source=cygwin/librsync-0.9.7.tar.gz \
	--with-libsqlite-source=cygwin/sqlite-2.8.16.tar.gz \
	--sysconfdir=$TRGDIR

make CFLAGS='-DREAL_DBDIR=\".\"'

ignore_dlls="KERNEL32.dll"
copy_dlls() {
	for dll in $( strings $1 | egrep '^[^ ]+\.dll$' | sort -u; )
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
copy_dlls $TRGDIR/csync2.exe

