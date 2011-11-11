#!/bin/bash

errors=0

current_year=$(date +%Y)

check() {
	years=`seq 2003 $current_year`
	for y in $(git log --date=short --pretty="format:%ad" -- $1 | cut -d- -f1 | sort -u)
	do
		years=`echo $years | sed "s,$y,,"`
		copyright=`perl -ne '/([*#]|^).*Copyright.*/ or next;
			s{\b(\d\d\d\d)\s*-\s*(\d\d\d\d)\b}
			 {join ", ", $1 .. $2}ge; print;' $1`
		case $copyright in
		*Copyright*$y*) :;;
		*)
			echo "Missing $y in $1."
			(( errors++ ))
			;;
		esac
	done
	for y in $years
	do
		case $copyright in
		*Copyright*$y*)
			echo "Bogus $y in $1."
			(( errors++ ))
			;;
		esac
	done
}

for f in ${1:- $(git ls-files | xargs grep -l Copyright)} ; do
	check $f
done

if [ $errors -ne 0 ]; then
	echo "Found $errors errors."
	exit 1
fi

exit 0

