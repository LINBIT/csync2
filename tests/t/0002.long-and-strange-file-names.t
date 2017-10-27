#!/bin/bash

# cd to canonical path of .
cd -P .

. $(dirname $0)/../include.sh

# That prepared some "node names" ($N1 .. $N9) and corresponding
# IPv4 addresses in /etc/hosts ($IP1 .. $IP9, 127.2.1.1 -- *.9)
# as well as variables $D1 .. $D9 to point to the respective sub tree
# of those "test node" instances.

# Cleanup does clean the data base and the test directories.
# You probably want to do a cleanup first thing in each test script.
# You may even do it several times throughout such a script.
cleanup

# populate $D1 
# ------------

f_trailing_space_pad_to_255=$(printf "%-255s" 'f trailing space pad to 255, contains umlauts äüöÄÖÜß and other special chars ☺ smiley : colon ^H ^? \\backslash single '\'' and double " quote')
long_dir=$(perl -e 'print((("D" x 255) . "/") x 16)')
create_tree()
{
	(
	set -xe
	date > "$D1/$f_trailing_space_pad_to_255"
	d=$D1/$long_dir
	d=${d::4095}
	d=${d%/}
	mkdir -p "$d"
	d=${d:: (4095 - 256)}
	d=${d%/}
	mkdir "$d"
	date > "$d/$f_trailing_space_pad_to_255"
	)
}

TEST	"create tree"	create_tree
TEST	"check"		csync2 -N $N1 -cr $D1
TEST	"list dirty"	csync2 -N $N1 -M

# compare and sync between both instances
# ---------------------------------------
TEST	"csync2 -uv"	csync2_u $N1 $N2
TEST	"diff -rq"	diff -rq $D1 $D2
