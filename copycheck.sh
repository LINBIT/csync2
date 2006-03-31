#!/bin/bash

errors=0

check() {
	if ! svn st $1 | grep -q '^?'; then
		years="2003 2004 2005 2006 2007 2008"
		for y in `svn log $1 | grep '^r[0-9]' | sed 's,.* \(200.\)-.*,\1,' | sort -u`
		do
			years=`echo $years | sed "s,$y,,"`
			if ! grep -q "\*.*Copyright.*$y" $1; then
				echo "Missing $y in $1."
				(( errors++ ))
			fi
		done
		for y in $years
		do
			if grep -q "\*.*Copyright.*$y" $1; then
				echo "Bogus $y in $1."
				(( errors++ ))
			fi
		done
	fi
}

for f in `grep -rl '\*.*Copyright' . | grep -v '/\.svn/'` ; do
	check $f
done

if [ $errors -ne 0 ]; then
	echo "Found $errors errors."
	exit 1
fi

exit 0

