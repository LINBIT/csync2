
sbin_PROGRAMS = csync2

csync2_SOURCES = action.c cfgfile_parser.y cfgfile_scanner.l check.c	\
                 checktxt.c csync2.c daemon.c db.c error.c getrealfn.c	\
                 groups.c rsync.c update.c urlencode.c conn.c

AM_YFLAGS = -d
BUILT_SOURCES = cfgfile_parser.h
CLEANFILES = cfgfile_parser.c cfgfile_parser.h cfgfile_scanner.c

AM_CPPFLAGS  = -D'DBDIR="$(localstatedir)/lib/csync2"'
AM_CPPFLAGS += -D'ETCDIR="$(sysconfdir)"'

install-data-local:
	$(mkinstalldirs) $(localstatedir)/lib/csync2

