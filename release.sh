#!/bin/bash
#
# Internal script for tagging a release in subversion and creating
# the source tar file.

PACKAGE=csync2
URL=http://svn.clifford.at/csync2

case "$1" in
  -*)
	echo "Usage: $0 newversion"
	;;
  '')
	svn ls $URL/tags | tr -d / | perl -pe '$x=$_; $x=~s/\n/\t/; print $x;
			s/(\d+)/sprintf"%04d",$1/eg;' | sort -k2 | cut -f1
	;;
  *)
	VERSION=$1
	set -ex

	date "+csync2 ($VERSION-1) unstable; urgency=low%n%n`
		`* New Upstream Version.%n%n -- Clifford Wolf `
		`<clifford.wolf@linbit.com>  %a, %d %b %Y `
		`%H:%M:%S %z%n" > debian/changelog.new
	cat debian/changelog >> debian/changelog.new
	mv debian/changelog.new debian/changelog
	svn commit -m "Added version $VERSION to debian changelog." \
			debian/changelog

	svn cp -m "Tagged version $VERSION" \
			$URL/trunk $URL/tags/$PACKAGE-$VERSION
	svn co $URL/tags/$PACKAGE-$VERSION ../$PACKAGE-$VERSION

	cd ../$PACKAGE-$VERSION
	perl -pi -e "s/SNAPSHOT/$VERSION/g" configure.in
	svn commit -m "Fixed version info in tag $VERSION" configure.in
	./autogen.sh; rm -rf aclocal.m4 autom4te.cache $( find -name .svn )
	rm -f release.sh # don't include this script in the source tar

	cd ..
	tar cvzf $PACKAGE-$VERSION.tar.gz $PACKAGE-$VERSION
	rm -rf $PACKAGE-$VERSION
	;;
esac

