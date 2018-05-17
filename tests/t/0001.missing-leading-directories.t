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

mkdir -p $D1/{a,b,c}/d/e
TEST	"init db 1"	csync2 -N $N1 -cIr $D1

touch -d "last week" $D1/{a,b,c}/d/e/f
TEST	"check"		csync2 -N $N1 -cr $D1
TEST	"list dirty"	csync2 -N $N1 -M

# compare and sync between both instances
# ---------------------------------------
TEST	"csync2 -uv"	csync2_u $N1 $N2
TEST	"diff -rq"	diff -rq $D1 $D2

# create conflicts
# ----------------

rm -rf $D2/{a,b,c}
touch $D1/{a,b,c}/d/e/f
TEST			"check again"	csync2 -N $N1 -cr $D1
TEST			"list dirty"	csync2 -N $N1 -M
TEST_EXPECT_EXIT_CODE 1	"csync2 -uv"	csync2_u $N1 $N2

# force 1 -> 2
# ----------------
TEST	"force"		csync2 -N $N1 -frv $D1
TEST	"csync2 -uv"	csync2_u $N1 $N2
TEST	"diff -rq"	diff -rq $D1 $D2
