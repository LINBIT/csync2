#!/bin/bash

. $(dirname $0)/../include.sh

# That prepared some "node names" ($N1 .. $N9) and corresponding
# IPv4 addresses in /etc/hosts ($IP1 .. $IP9, 127.2.1.1 -- *.9)
# as well as variables $D1 .. $D9 to point to the respective sub tree
# of those "test node" instances.

# Cleanup does clean the data base and the test directories.
# You probably want to do a cleanup first thing in each test script.
# You may even do it several times throughout such a script.
cleanup

# A "Test" needs
#	* an expectation (exit code)
#	* a description,
#	* a "simple command" with optional arguments.
# TEST is shorthand for TEST_EXPECT_EXIT_CODE 0

TEST_EXPECT_EXIT_CODE 2 "list non-existent db"	csync2 -L -N $N1
TEST_EXPECT_EXIT_CODE 2 "list non-existent db"	csync2 -L -N $N2

# You are free to do whatever you want
# in preparation for the next test,
# remove some files, create some files, change some content

# populate $D1
# ------------

mkdir -p $D1/a
TEST	"init db 1"	csync2 -N $N1 -cIr $D1

seq 1000 | tee $D1/a/f >/dev/null
touch -d "last week" $D1/a/f
TEST	"check"		csync2 -N $N1 -cr $D1
TEST	"list dirty"	csync2 -N $N1 -M

# compare and sync between both instances
# ---------------------------------------
TEST	"csync2 -uv"	csync2_u $N1 $N2
TEST	"diff -rq"	diff -rq $D1 $D2

# create conflicts
# ----------------

seq 1000 | tee $D1/a/f >/dev/null
seq  999 | tee $D2/a/f >/dev/null
TEST			"check again"	csync2 -N $N1 -cr $D1
TEST			"list dirty"	csync2 -N $N1 -M

# Verify the current diff:
csync2_T()
{
	csync2 -N $1 -iii &
	sleep 1
	tmp=$( csync2 -N $2 -T )
	[[ "$tmp" = "$3" ]]
}

csync2_TT()
{
	csync2_daemon -N $1 -ii &
	d_pid=$!
	sleep 1
	tmp=$( csync2 -N $2 -TT $2 $1 $3 )
	kill $d_pid && wait
	[[ "$tmp" = "$4" ]]
}

TEST	"csync2 -T"	csync2_T $N1 $N2 "\
R	2.csync2.test	1.csync2.test	%demodir%
X	2.csync2.test	1.csync2.test	%demodir%/a/f"

TEST	"csync2 -TT"	csync2_TT $N1 $N2 $D2/a/f "\
--- 1.csync2.test:%demodir%/a/f
+++ 2.csync2.test:%demodir%/a/f
@@ -997,4 +997,3 @@
 997
 998
 999
-1000"

TEST_EXPECT_EXIT_CODE 1	"csync2 -uv"	csync2_u $N1 $N2

# force 1 -> 2
# ----------------
TEST	"force"		csync2 -N $N1 -frv $D1
TEST	"csync2 -uv"	csync2_u $N1 $N2
TEST	"diff -rq"	diff -rq $D1 $D2
