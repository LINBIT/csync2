
CC = gcc

CFLAGS  = -Os -ggdb -Wall -Winline
LDFLAGS = -lsqlite -lrsync

prefix = /usr/local

# let yacc create a t.tab.h
YFLAGS = -d

all: csync2

clean:
	rm -f *.o *~ y.tab.h csync2 core

csync2: csync2.o db.o error.o config_parser.o config_scanner.o \
        check.o update.o daemon.o getrealfn.o rsync.o urlencode.o \
	checktxt.o groups.o action.o

install: csync2
	install -d /var/lib/csync2
	install -D csync2 $(prefix)/sbin/csync2

