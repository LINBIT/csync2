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

# You may or may not want to re-prepare the config file every time.
# Feel free to reuse in later test scripts what is generated here:
#prepare_cfg_file || bail_out "Unable to create config file"

# Which sets up a prefix %demodir% for each of the "instances",
# and has these directives:
#	include %demodir%/;
#	exclude %demodir%/exclude;
# to be able to do some basic checks.


# A "Test" needs
#	* an expectation (exit code)
#	* a description,
#	* a "simple command" with optional arguments.
# TEST is shorthand for TEST_EXPECT_EXIT_CODE 0

# ALL tests in this just have to work.
# bail out at the first failure
BAIL_OUT_EARLY=1

TEST_EXPECT_EXIT_CODE 1 "usage output"		csync2
TEST_EXPECT_EXIT_CODE 1 "host not mentioned"	csync2 -L -N does.not.exist
TEST_EXPECT_EXIT_CODE 2 "list non-existent db"	csync2 -L -N $N1


# You are free to do whatever you want
# in preparation for the next test,
# remove some files, create some files, change some content

# However, DO NOT use redirection or non-simple commands directly on a "TEST"
# command line. If you need that, use eval, like so:
# TEST "short description" eval 'some | involved | pipe || other > with 2> redirection'

# populate $D1
# ------------

mkdir -p $D1/a
touch $D1/a/f

# The simple command given to TEST is executed in a temp directory,
# and its stdout and stderr are redirected to files in there.
# So don't rely on the location of "."

# Note: intentionally not using -I here,
# I want everything to be flagges as dirty.
TEST	"init db 1"	csync2 -N $N1 -crvv $D1
TEST	"list db 1"	csync2 -N $N1 -L

# populate $D2
# ------------

mkdir -p $D2/b
TEST	"touch it 2"	touch -d "last week" $D2/b/f
TEST	"init db 2"	csync2 -N $N2 -cIrvv $D2

# If you need more than a "simple" command,
# define a shell function, and test that.

# expect this number of records in the "known" database now:
t() { [[ $(csync2 -N $N2 -L | wc -l) = 3 ]] ; }
TEST	"list db 2"	t

# But since it was an "init" run (-I),
# nothing should be dirty.
TEST_EXPECT_EXIT_CODE 2	"nothing dirty"	csync2 -N $N2 -M

# This time, we expect it to populate the dirty table,
# and -M should have an exit code of 0.
TEST	"touch again 2"	eval "date > $D2/b/f"
TEST	"check db 2"	csync2 -N $N2 -crvv $D2
TEST	"list dirty 2"	csync2 -N $N2 -M


# compare and sync between both instances
# ---------------------------------------

# Verify the current diff:
csync2_T()
{
	csync2 -N $1 -iii &
	tmp=$( csync2 -N $2 -T )
	[[ "$tmp" = "$3" ]]
}

TEST	"csync2 -T"	csync2_T $N1 $N2 "\
R	2.csync2.test	1.csync2.test	%demodir%/a
R	2.csync2.test	1.csync2.test	%demodir%/a/f
L	2.csync2.test	1.csync2.test	%demodir%/b
L	2.csync2.test	1.csync2.test	%demodir%/b/f"


# sync up
TEST	"csync2 -uv"	csync2_u $N1 $N2
TEST	"csync2 -uv"	csync2_u $N2 $N1

# and now, they should be in sync
TEST	"csync2 -T"	csync2_T $N1 $N2 ""
TEST	"diff -rq"	diff -rq $D1 $D2

# and checking again should not redirty anything.
TEST	"check 1"	csync2 -N $N1 -crvv $D1
TEST	"check 2"	csync2 -N $N2 -crvv $D2
TEST_EXPECT_EXIT_CODE 2	"nothing dirty"	csync2 -N $N1 -M
TEST_EXPECT_EXIT_CODE 2	"nothing dirty"	csync2 -N $N2 -M
TEST	"csync2 -T"	csync2_T $N1 $N2 ""

# remove some stuff
rm -rf $D1/*
TEST	"check post rm"	csync2 -N $N1 -crv $D1
TEST	"csync2 -uv"	csync2_u $N1 $N2
TEST	"diff -rq"	diff -rq $D1 $D2


############
# This was the absolutely basic functionality test.
# If any of the tests in this file failed,
# do not even attempt further testing.
#
# Keep this last.
[[ $__test_ok_cnt = $__test_total_cnt ]] ||
	bail_out "Some basic sanity checks failed, no use testing anything else."
