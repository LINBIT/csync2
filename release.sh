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
		`  * New Upstream Version.%n%n -- Clifford Wolf `
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
	perl -pi -e "s/SNAPSHOT/$VERSION/g" configure.ac
	perl -pi -e "s/SNAPSHOT/$VERSION/g" csync2.spec
	svn rm release.sh; sleep 2
	svn commit -m "Fixed version info in tag $VERSION"
	./autogen.sh; rm -rf autom4te.cache $( find -name .svn )

	cd ..
	tar cvzf $PACKAGE-$VERSION.tar.gz \
		--owner=0 --group=0 $PACKAGE-$VERSION
	rm -rf $PACKAGE-$VERSION
	;;
esac

