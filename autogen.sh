#!/bin/bash -x

aclocal-1.7
autoheader
automake-1.7 --add-missing --copy
autoconf

if [ "$1" = clean ]; then
	./configure && make distclean
	rm -rf librsync[.-]* libsqlite.* sqlite-*
	rm -rf configure Makefile.in depcomp stamp-h.in
	rm -rf mkinstalldirs config.h.in autom4te.cache
	rm -rf missing aclocal.m4 install-sh *~
	rm -rf config.guess config.sub
fi

