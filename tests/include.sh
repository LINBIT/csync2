#!/bin/bash

################################################################
# This is a small collection of helper functions to produce
# TAP, as in http://en.wikipedia.org/wiki/Test_Anything_Protocol
# excercised by prove(1)
#
#
# Minimal Introduction
# --------------------
#
# Each test script (*/*.t) will include this file,
# which will generate a "testing plan
# http://search.cpan.org/~petdance/Test-Harness-2.64/lib/Test/Harness/TAP.pod#THE_TAP_FORMAT
#
# TAP's general format is:
#
#     1..N
#     ok 1 Description # Directive
#     # Diagnostic
#     ....
#     not ok 46 Description
#     ok 47 Description
#     ok 48 Description
#     not ok 49 # TODO not yet implemented
#     not ok 50 # SKIP needs 77bit architecture
#     more tests....
#
# Skipping whole test files:
#
#     1..0 # Skipped: WWW::Mechanize not installed
#
# Bail out!
#
# As an emergency measure a test script can decide that further tests are useless
# (e.g. missing dependencies) and testing should stop immediately. In that case
# the test script prints the magic words
#
#     Bail out! MySQL is not running.
#
################################################################

# You better not use fd 44 for anything else...

exec 44>&1
bail_out()
{
	>&44 echo "Bail out! $*"
	# no exit trap on bailout.
	trap - EXIT
	exit 1
}

dbg()
{
	local lvl=$1
	shift
	(( $DEBUG_LEVEL < $lvl )) && return
	>&44 echo "##<$lvl># $*"
}

# Sometimes (e.g. when doing "-TT"), single shot ("-iii") is not good enough.
# But we should be able to kill that daemon without using "killall".
# So several layers of indirection are bad,
# the caller would not know whom to kill really.
csync2_daemon()
{
	dbg 1 "CSYNC2_SYSTEM_DIR=$CSYNC2_SYSTEM_DIR csync2 -D $CSYNC2_DATABASE $*"
	exec "$SOURCE_DIR/csync2" -D "$CSYNC2_DATABASE" "$@"
}

csync2()
{
	local ex
	dbg 1 "CSYNC2_SYSTEM_DIR=$CSYNC2_SYSTEM_DIR csync2 -D $CSYNC2_DATABASE $*"
	command "$SOURCE_DIR/csync2" -D "$CSYNC2_DATABASE" "$@"
	ex=$?
	dbg 1 "exit code: $ex"
	return $ex
}

## Does NOT remove the config file
cleanup()
{
	local host
	local i
	case $CSYNC2_DATABASE in
		sqlite://*|sqlite2://*|sqlite3://*|/*)
			mkdir -p "$CSYNC2_DATABASE/"
			rm -f "$CSYNC2_DATABASE/"*.csync2.test.db*
			;;
		pgsql://csync2:csync2@*/)
			host=${CSYNC2_DATABASE#pgsql://csync2:csync2@}
			host=${host%/}
			[[ $host = *[/\ ]* ]] && bail_out "cannot use $CSYNC2_DATABASE"
			for i in {1..9}; do
				psql "host=$host user=csync2 password=csync2 dbname=postgres" \
					-c "DROP DATABASE IF EXISTS csync2_${i}_csync2_test;" \
				|| bail_out "could not cleanup postgres database $i"
			done
			;;
		mysql://csync2:csync2@*/)
			host=${CSYNC2_DATABASE#mysql://csync2:csync2@}
			host=${host%/}
			[[ $host = *[/\ ]* ]] && bail_out "cannot use $CSYNC2_DATABASE"
			for i in {1..9}; do
				mysql --host=$host --user=csync2 --password=csync2 mysql \
					-e "DROP DATABASE IF EXISTS csync2_${i}_csync2_test;" \
				|| bail_out "could not cleanup mysql database $i"
			done
			;;
		*)
			bail_out "unsupported CSYNC2_DATABASE setting $CSYNC2_DATABASE"
			;;
	esac
	rm -rf "$TESTS_DIR/"{1..9}
	mkdir -p "$TESTS_DIR/"{1..9}
}

prepare_etc_hosts_bring_up_ips()
{
	for i in {1..9}; do
		eval N$i=$i.csync2.test
		eval IP$i=127.2.1.$i
		eval D$i=$TESTS_DIR/$i
		grep -qFxe "127.2.1.$i $i.csync2.test" /etc/hosts \
		||    echo "127.2.1.$i $i.csync2.test" >> /etc/hosts
		ip -o -f inet a s dev lo | grep -qFe " 127.2.1.$i/" ||
		ip a add dev lo 127.2.1.$i/24
	done
}

# generates a new config file with proper %demodir% prefixes
prepare_cfg_file()
{
	local CFG="$CSYNC2_SYSTEM_DIR/csync2.cfg";
	if test -e "$CFG" ; then
		dbg 1 "$CFG already in place, using it as is"
		return
	fi

	dbg 0 "generating $CFG"
	cat > "$CFG" <<-___
	group demo
	{
		host 1.csync2.test;
		host 2.csync2.test;

		key csync2.key_demo;

		include %demodir%;
		exclude %demodir%/e;
	}

	prefix demodir
	{
		on 1.csync2.test: $TESTS_DIR/1;
		on 2.csync2.test: $TESTS_DIR/2;
		on 3.csync2.test: $TESTS_DIR/3;
		on 4.csync2.test: $TESTS_DIR/4;
		on 5.csync2.test: $TESTS_DIR/5;
		on 6.csync2.test: $TESTS_DIR/6;
		on 7.csync2.test: $TESTS_DIR/7;
		on 8.csync2.test: $TESTS_DIR/8;
		on 9.csync2.test: $TESTS_DIR/9;
	}

	nossl * *;
___
}

__test_result_header()
{
	printf "# %s %s %s\n" $TEST_CALL_STACK

	echo "# =========== $description = trace = {{{3"
	sed -e 's/^/# -x : /' < "$TESTS_TMP_DIR/xtrace"
	echo "# =========== $description = err = {{{3"
	sed -e 's/^/# err: /' < "$TESTS_TMP_DIR/err"
	echo "# =========== $description = out = {{{2"
	sed -e 's/^/# out: /' < "$TESTS_TMP_DIR/out"
	echo "# =========== $description = exit:$result = expected:$expected_result_code }}}1"
}

__test_not_ok()
{
	__test_result_header
	echo "not ok $__test_cur - $description"
	let __test_not_ok_cnt++
	return 1
}

__test_ok()
{
	(( $DEBUG_LEVEL > 1 )) && __test_result_header
	echo "ok $__test_cur - $description"
	let __test_ok_cnt++
	return 0
}

_TEST()
{
	let __test_cur++
	local expected_result_code=$1; shift
	local description=$1; shift
	local result="N/A"

	export TEST_DESCRIPTION="$0 $description"
	export TEST_CALL_STACK="$(i=0; while caller $i; do let i++; done)"

	# truncate stdout/stderr/xtrace capture files
	: >"$TESTS_TMP_DIR/out" >"$TESTS_TMP_DIR/err" >"$TESTS_TMP_DIR/xtrace"

	echo "# =========== "$test_line" {{{1"
	if [[ $# = 0 ]] ; then
		__test_not_ok	"Bad test line"
		bail_out "You need to fix the test scripts first"
		return 1
	fi

	(
		set -e
		cd "$TESTS_TMP_DIR"
		exec >>out 2>>err 45>>xtrace
		BASH_XTRACEFD=45
		: $BASHPID
		set +e -x
		"$@"
	)
	result=$?

	if [[ ${GIVE_ME_A_SHELL_AFTER_EACH_TEST-} ]] && test -t 0 && test -t 1 ; then
		(
			cd "$TESTS_TMP_DIR"
			echo "-----------"
			echo "CSYNC2 TEST debug shell ..."
			echo "$0 -- "$test_line
			echo "To abort further tests: touch bailout"
			echo "                        -------------"
			export -f csync2 dbg
			bash
		)
		test -e "$TESTS_TMP_DIR/bailout" && bail_out "$TESTS_TMP_DIR/bailout"
	fi

	if [[ $result = $expected_result_code ]] ; then
		__test_ok
	else
		__test_not_ok
		[[ ${BAIL_OUT_EARLY:-} ]] && bail_out "$BAIL_OUT_EARLY"
	fi
}

TEST()
{
	local test_line="TEST '$1' ${*:2}"
	_TEST 0 "$@"
}

TEST_EXPECT_EXIT_CODE()
{
	local test_line="TEST_EXPECT_EXIT_CODE [expected:$1] '$2' ${*:3}"
	_TEST "$@"
}

TEST_BREAK()
{
	GIVE_ME_A_SHELL_AFTER_EACH_TEST=1 TEST "$@"
}

require()
{
	if ! "$@"; then
		echo "# $*"
		echo "1..0 # Skipped: test requirements failed"
		exit 1
	fi
}

#############################################################################
## some helper functions that are likely to be reused by many test scripts ##
#############################################################################

csync2_u()
{
	local tmp kid now client_exit nc_exit server_exit
	if csync2 -N $1 -M > /dev/null; then
		csync2 -N $2 -iii -vvv &
		kid=$!

		# try to avoid the connect-before-listen race,
		# wait for the expected listening socket to show up,
		# with timeout using bash magic $SECONDS.
		now=$SECONDS
		while ! ss -tnl src $2:csync2 | grep -q ^LISTEN; do
			kill -0 $kid
			(( SECONDS - now < 2 ))
			sleep 0.1
		done

		csync2 -N $1 -uvvv
		client_exit=$?

		if ss -tnl src $2:csync2 | grep -q ^LISTEN; then
			# server still alive?
			# then no connection was made...
			# attempt to do one now.
			tmp=$(nc $2 $CSYNC2_PORT <<<"BYE" )
			nc_exit=$?

			[[ $nc_exit = 0 ]] || kill $kid
		else
			nc_exit=1
		fi
		wait $kid
		server_exit=$?
		[[ $client_exit = 0 && $server_exit = 0 && $nc_exit != 0 ]]
	else
		echo "Apparently nothing dirty on $1, not starting server on $2"
		# but still do the csync2 -u ...
		csync2 -N $1 -uvvv
	fi
}


################################
##  Basic setup code follows  ##
################################

set -u

if [[ $BASH_SOURCE == include.sh ]] ; then
	TESTS_DIR=$PWD
elif [[ $BASH_SOURCE == */include.sh ]]; then
	TESTS_DIR=${BASH_SOURCE%/include.sh}
else
	bail_out "Sorry, I'm confused..."
fi

TESTS_DIR=$(cd "$TESTS_DIR" && pwd )
SOURCE_DIR=$(cd "$TESTS_DIR/.." && pwd )

: CSYNC2_PORT	    ${CSYNC2_PORT:=30865}
: CSYNC2_SYSTEM_DIR ${CSYNC2_SYSTEM_DIR:=$TESTS_DIR/etc}
: CSYNC2_DATABASE   ${CSYNC2_DATABASE:=$TESTS_DIR/db}

: DEBUG_LEVEL=${DEBUG_LEVEL:=0}

__test_cur=0
__test_ok_cnt=0
__test_not_ok_cnt=0

case "$0" in
-bash|bash)
	echo >&2 "Interactive mode??"
	__test_total_cnt=0
	export -f csync2
	;;
*)

	TESTS_TMP_DIR=$(mktemp -d "$TESTS_DIR/tmp.XXX")
	trap 'rm -rf "$TESTS_TMP_DIR"' EXIT

	__test_total_cnt=$(grep -Ece '^[[:space:]]*\<(TEST|TEST_EXPECT_EXIT_CODE|TEST_BREAK)\>' $0)
	;;
esac

if [[ $# -gt 1 ]] && [[ $1 = require ]]; then
	"$@"
fi

export CSYNC2_SYSTEM_DIR CSYNC2_DATABASE
export TESTS_DIR TESTS_TMP_DIR SOURCE_DIR
prepare_etc_hosts_bring_up_ips
prepare_cfg_file

# The Plan
echo 1..$__test_total_cnt
