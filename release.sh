#!/bin/bash

PACKAGE=csync2
URL=http://svn.clifford.at/csync2

case "$1" in
  -*)
	echo "Usage: $0 newversion"
	;;
  '')
	svn ls $URL/tags
	;;
  *)
	VERSION=$1; set -ex; cd ..
	svn cp -m "Tagged version $VERSION" \
			$URL/trunk $URL/tags/$PACKAGE-$VERSION
	svn co $URL/tags/$PACKAGE-$VERSION; cd $PACKAGE-$VERSION
	perl -pi -e "s/SNAPSHOT/$VERSION/g" configure.in
	svn commit -m "Fixed version info in tag $VERSION" configure.in
	./autogen.sh; rm -rf aclocal.m4 autom4te.cache $( find -name .svn )
	cd ..; tar cvzf $PACKAGE-$VERSION.tar.gz $PACKAGE-$VERSION
	rm -rf $PACKAGE-$VERSION
	;;
esac

