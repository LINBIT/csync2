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

# populate $D1 
# ------------

create_some_files()
{
	date | tee $D1/1.txt $D1/hello.txt
	date | tee $D1/1.TXT $D1/Hello.txt
}

TEST	"create some files"	create_some_files
TEST	"check"		csync2 -N $N1 -cr $D1
TEST	"list dirty"	csync2 -N $N1 -M

# compare and sync between both instances
# ---------------------------------------
TEST	"csync2 -uv"	csync2_u $N1 $N2
TEST	"diff -rq"	diff -rq $D1 $D2

# redirty only one
date | tee -a $D1/1.txt $D1/hello.txt
TEST	"check"		csync2 -N $N1 -cr $D1
t() { [[ $(csync2 -N $N1 -M | wc -l) = 2 ]] ; }
TEST	"only lower case should be dirty"	t
