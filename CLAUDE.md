# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Csync2 is an asynchronous cluster file synchronization tool written in C. A single binary `csync2` runs both as a CLI client (check/mark/update) and as a network daemon (via inetd, systemd socket activation, or standalone `-ii` / `-iii`). Hosts exchange dirty-file state over TCP/30865, authenticate with a pre-shared key and (optionally) GnuTLS, and transfer file contents with librsync deltas.

Upstream: https://github.com/LINBIT/csync2/. Mailing list: csync2@lists.linbit.com.

## Build

Autotools-based, GNU bison + flex for the config parser.

```sh
./autogen.sh        # runs aclocal/autoheader/automake/autoconf then ./configure with default flags
make
./autogen.sh clean  # full distclean including generated autotools artifacts
```

`autogen.sh`'s default `./configure` flags (`--enable-sqlite3 --enable-mysql --enable-postgres --enable-systemd`, `--prefix=/usr`, `--sysconfdir=/etc/csync2`, `--localstatedir=/var`) match how distros build it. Re-run `./configure` by hand to change them. At least one DB backend must be enabled (`configure.ac` enforces this).

Build deps: `librsync`, `gnutls` (`>= 2.6.0`, optional but on by default), `libsqlite3-dev`, `libmysqlclient-dev`, `libpq-dev`, `bison`, `flex`. MySQL and PostgreSQL libs are `dlopen`'d at runtime (see `dl.h`, `LIBMYSQLCLIENT_SO` / `LIBPQ_SO` in `config.h`), so packages can be built once and run on systems missing those `.so` files.

On Ubuntu 24.04 the apt packages are `librsync-dev libgnutls28-dev libsqlite3-dev libmysqlclient-dev libpq-dev bison flex pkg-config`.

Two warnings are currently tolerated on a clean build and are *not* to be silenced piecemeal — they are tracked subtopics in the overhaul plan:

- `cfgfile_parser.y: POSIX Yacc does not support %expect` — addressed by subtopic 2f (parser cleanup).
- `conn.c:conn_response conflicting types due to enum/integer mismatch` — to be addressed by subtopic 2b (TLS modernization) which is rewriting that area anyway.

## Tests

TAP-format tests under `tests/t/*.t`, driven by `prove(1)`. They are integration tests that exercise the real binary, so they need root and they modify the host:

- adds entries `127.2.1.{1..9} {1..9}.csync2.test` to `/etc/hosts`
- brings up `127.2.1.{1..9}/24` on `lo`
- by default uses sqlite under `$TESTS_DIR/db`

Run from the repo root after `make`:

```sh
sudo prove -v tests/t/                     # all tests
sudo prove -v tests/t/0000.basic.t         # single test
sudo CSYNC2_DATABASE=pgsql://csync2:csync2@localhost/ prove -v tests/t/   # other backend
sudo DEBUG_LEVEL=2 GIVE_ME_A_SHELL_AFTER_EACH_TEST=1 prove -v tests/t/0000.basic.t   # interactive
```

Helpers in `tests/include.sh`: `TEST`, `TEST_EXPECT_EXIT_CODE`, `TEST_BREAK`, `csync2`, `csync2_daemon`, `cleanup`, `prepare_cfg_file`. Tests bail with `bail_out` on prereq failure. Honored env vars: `CSYNC2_PORT`, `CSYNC2_SYSTEM_DIR`, `CSYNC2_DATABASE`, `DEBUG_LEVEL`, `BAIL_OUT_EARLY`, `GIVE_ME_A_SHELL_AFTER_EACH_TEST`.

## Architecture

### Operating modes (csync2.c)

Top of `csync2.c` defines the `MODE_*` enum and the giant `getopt` loop in `main()` dispatches each mode. The user-facing summary is in `help()` (run `csync2` with no args). Key modes:

- `-c file...` / bare `-c` — check files (or all hinted files) and mark dirty in DB → `check.c::csync_check`
- `-u` — push dirty entries to peers → `update.c::csync_update`
- `-x` — combined check + update (the typical cron job)
- `-T` / `-TT` — in-sync test, optionally with diffs
- `-i` / `-ii` / `-iii` — inetd mode / standalone server / one-shot standalone server → `csync_server_loop` + `daemon.c::csync_daemon_session`
- `-k file` — generate a pre-shared key
- `-H` / `-L` / `-M` / `-S` — list hint/file/dirty/sync tables

### Module map

- `csync2.c` — `main`, argv parsing, mode dispatch, server bind/accept loop.
- `daemon.c` — peer-facing protocol: each connection runs `csync_daemon_session`, which reads commands (`sig`, `patch`, `mkdir`, `del`, `mv`, `flush`, …) and replies using the response strings.
- `update.c` — client-side: walks dirty DB, opens connection (`connect_to_host`), drives the protocol, calls librsync.
- `check.c` — walks the filesystem, computes checktxt, inserts into `file` / `dirty` / `hint` tables. `csync_mark` adds dirty rows.
- `checktxt.c` — produces the textual signature (`v1:mtime=…:mode=…:…`) that is the on-disk equivalence test.
- `groups.c` — interprets the parsed config: `csync_find_next`, `csync_match_file`, `csync_match_file_host`, `csync_find_peers`, `csync_key`, `csync_perm` (peer ACL).
- `cfgfile_parser.y` + `cfgfile_scanner.l` — bison/flex grammar for `csync2.cfg`. Populates the globals `csync_group`, `csync_prefix`, `csync_nossl` declared in `csync2.h`. `AM_YFLAGS = -d` produces `cfgfile_parser.h` (a `BUILT_SOURCES` entry).
- `action.c` — runs the `action { exec ... }` commands configured per group after a successful local change.
- `conn.c` — socket I/O, optional GnuTLS (`HAVE_LIBGNUTLS`), buffered `conn_gets`/`conn_printf`, and the `connection_response` <-> string mapping (`conn_response`, `conn_response_to_enum`).
- `rsync.c` — wraps librsync (`csync_rs_sig`, `csync_rs_delta`, `csync_rs_patch`) plus `mkpath`, `split_dirname_basename`.
- `db.c` — high-level SQL: `SQL` / `SQL_BEGIN` / `SQL_END` macros (defined in `csync2.h`), transaction batching with SIGALRM-driven auto-COMMIT, busy retry, and `db_blocking_mode` / `db_sync_mode` knobs (`-B`, `-A`).
- `db_api.c` + `db_sqlite.c` / `db_sqlite2.c` / `db_mysql.c` / `db_postgres.c` — per-backend implementations of the vtable in `db_api.h` (`struct db_conn_t` / `db_stmt_t` are arrays of function pointers). `csync_parse_url` in `db_api.c` parses `sqlite://`, `mysql://`, `pgsql://` URLs.
- `urlencode.c` / `prefixsubst.c` — small string helpers, both backed by **ring buffers**: the return value is only valid until the buffer wraps. The header comment warns about this; do not hold onto the returned `const char *` across many calls.
- `error.c` — `csync_debug` (level-gated, can go to syslog when `-l`), `csync_fatal`, timestamping.
- `getrealfn.c` — resolves the path symlinks-and-all without resolving the final basename (config rejects symlink-containing paths).

### Connection protocol (csync2.h ~line 118)

Text-based. Every successful reply starts with `OK (`; anything else is an error. The enum `connection_response` is **internal only**; only the strings travel over the wire (`conn_response()` maps enum→wire, `conn_response_to_enum()` maps back). When adding new responses keep the OK ones in the `CR_OK_…` block (before `CR_ERROR`) and error ones after, and ensure every OK string starts with `OK (`.

### Load-bearing invariants

A few non-obvious invariants the code relies on; do not break them without coordinated changes:

- `groups.c:csync_find_next` skips groups whose `myname` is unset (foreign groups). Every downstream consumer (`csync_key`, `csync_perm`, `csync_match_file_host`) walks groups via `csync_find_next` and therefore assumes any group it sees has `myname` set and (consequently) `key` loaded. Anything that walks `csync_group` directly must replicate this filter or risk NULL deref on `g->key`.
- The receive-side daemon loop (`daemon.c:csync_daemon_session`) currently runs `csync_file_update` and the "Updated %s from %s" log unconditionally after each command, even when `cmd_error` is set. The dirty row is cleared on what may have been a failed receive. This is a known wart; the cosmetic-and-DB symptom is real, the principled fix belongs in subtopic 2a (protocol redesign).

### SQL hygiene

There is no prepared-statement parameter binding in the high-level macros — query strings are built with `printf` formatting. **Every untrusted value passed into `SQL(...)` or `SQL_BEGIN(...)` must go through `url_encode()` first** (and decoded with `url_decode()` on read). That is the project's escaping mechanism. Look at any existing query for the pattern, e.g. `daemon.c::csync_file_update`.

### Config file layout

`/etc/csync2/csync2.cfg` (overridable with `-C name` → `csync2_<name>.cfg`, or `CSYNC2_SYSTEM_DIR`). Contains `group { host ...; key ...; include ...; exclude ...; action { ... } }` blocks plus optional `prefix`, `nossl`, `ignore` directives. See `csync2.cfg` (example), `doc/csync2.adoc` (reference), `doc/csync2-quickstart.adoc` (intro). The `%name%` syntax in include/exclude paths is resolved per-host via `prefix` blocks (see `prefixsubst.c`).

## Overhaul in flight

A breaking next-major-release overhaul is being planned on branch `overhaul`. Read both before suggesting non-trivial changes — many obvious refactors are already accounted for there, and acting against the plan creates churn:

- `doc/overhaul-prompt.md` — goals, ground rules, subtopics 2a–2f.
- `doc/overhaul-plan.md` — triage of every open issue/PR, design notes per subtopic, test strategy, phased plan-of-plans.

A second branch `master-cleanup` carries Phase 0 (small standalone fixes targeted at master) and the tag `bak-master-cleanup` preserves the pre-style-rewrite tip of that branch.

A reference rsync source checkout lives at `~/src/rsync` (3.5.0dev). It's the precedent for "how should we handle this" questions on metadata, file-list streaming, atomic receive, sparse files, delete handling.

## Versioning / release

- `configure.ac` and `csync2.spec` carry the version (e.g. `2.1-0rc1`); `release.sh <version>` is the helper that bumps both plus `debian/changelog` and prints the suggested `git commit` / `git tag` / `tar` commands (it does NOT execute them — paste the lines you want).
- `csync2.h::DB_SCHEMA_VERSION` bumps when the on-disk DB schema changes; `db_upgrade_to_schema` per-backend handles migrations.
