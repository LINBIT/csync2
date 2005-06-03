
sbin_PROGRAMS = csync2

csync2_SOURCES = action.c cfgfile_parser.y cfgfile_scanner.l check.c	\
                 checktxt.c csync2.c daemon.c db.c error.c getrealfn.c	\
                 groups.c rsync.c update.c urlencode.c conn.c

AM_YFLAGS = -d
BUILT_SOURCES = cfgfile_parser.h
CLEANFILES = cfgfile_parser.c cfgfile_parser.h cfgfile_scanner.c \
             private_librsync private_libsqlite

AM_CFLAGS=
AM_LDFLAGS=

if PRIVATE_LIBRSYNC
  BUILT_SOURCES += private_librsync
  AM_CFLAGS += -I$(shell test -f librsync.dir && cat librsync.dir || echo ==librsync==)
  AM_LDFLAGS += -L$(shell test -f librsync.dir && cat librsync.dir || echo ==librsync==)
  LIBS += -lprivatersync
endif

if PRIVATE_LIBSQLITE
  BUILT_SOURCES += private_libsqlite
  AM_CFLAGS += -I$(shell test -f libsqlite.dir && cat libsqlite.dir || echo ==libsqlite==)
  AM_LDFLAGS += -L$(shell test -f libsqlite.dir && cat libsqlite.dir || echo ==libsqlite==)
  LIBS += -lprivatesqlite
endif

AM_CPPFLAGS  = -D'DBDIR="$(localstatedir)/lib/csync2"'
AM_CPPFLAGS += -D'ETCDIR="$(sysconfdir)"'

install-data-local:
	$(mkinstalldirs) $(localstatedir)/lib/csync2

cert:
	openssl genrsa -out $(sysconfdir)/csync2_ssl_key.pem 1024
	openssl req -new -key $(sysconfdir)/csync2_ssl_key.pem -out $(sysconfdir)/csync2_ssl_cert.csr
	openssl x509 -req -days 600 -in $(sysconfdir)/csync2_ssl_cert.csr \
		-signkey $(sysconfdir)/csync2_ssl_key.pem -out $(sysconfdir)/csync2_ssl_cert.pem


## hack for building private librsync and private libsqlite ##

private_librsync:
	tar xvzf $(librsync_source_file) | cut -f1 -d/ | sed '1 p; d;' > librsync.dir
	test -s librsync.dir && cd $$( cat librsync.dir ) && ./configure --enable-static --disable-shared
	make -C $$( cat librsync.dir )
	cp $$( cat librsync.dir )/.libs/librsync.a $$( cat librsync.dir )/libprivatersync.a
	touch private_librsync

private_libsqlite:
	tar xvzf $(libsqlite_source_file) | cut -f1 -d/ | sed '1 p; d;' > libsqlite.dir
	test -s libsqlite.dir && cd $$( cat libsqlite.dir ) && ./configure --enable-static --disable-shared
	make -C $$( cat libsqlite.dir )
	cp $$( cat libsqlite.dir )/.libs/libsqlite.a $$( cat libsqlite.dir )/libprivatesqlite.a
	touch private_libsqlite

