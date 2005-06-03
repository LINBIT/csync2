#!/bin/bash

if ! [ -f sqlite-2.8.16.tar.gz ]; then
	wget http://www.sqlite.org/sqlite-2.8.16.tar.gz -O sqlite-2.8.16.tar.gz
fi

if ! [ -f librsync-0.9.7.tar.gz ]; then
	wget http://mesh.dl.sourceforge.net/sourceforge/librsync/librsync-0.9.7.tar.gz -O librsync-0.9.7.tar.gz
fi

cd ..
mkdir -p /cygdrive/c/csync2

./configure \
	--with-librsync-source=cygwin/librsync-0.9.7.tar.gz \
	--with-libsqlite-source=cygwin/sqlite-2.8.16.tar.gz \
	--sysconfdir=/cygdrive/c/csync2

make

cp -v csync2.exe /cygdrive/c/csync2/
for dll in $( strings csync2.exe | grep '\.dll$' | grep -v KERNEL32; ); do
	cp -v /bin/$dll /cygdrive/c/csync2/
done

