#!/bin/bash

aclocal
autoheader
automake --add-missing --copy
autoconf

if [ "$1" = clean ]; then
	./configure && make distclean; set -x
	rm -rf configure Makefile.in depcomp stamp-h.in
	rm -rf mkinstalldirs config.h.in autom4te.cache
	rm -rf missing aclocal.m4 install-sh *~
fi

