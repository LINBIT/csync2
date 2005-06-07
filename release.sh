#!/bin/bash
#
# csync2 - cluster synchronisation tool, 2nd generation
# LINBIT Information Technologies GmbH <http://www.linbit.com>
# Copyright (C) 2004, 2005  Clifford Wolf <clifford@clifford.at>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
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
	svn rm release.sh
	perl -pi -e "s/SNAPSHOT/$VERSION/g" configure.ac
	perl -pi -e "s/SNAPSHOT/$VERSION/g" csync2.spec
	svn commit -m "Fixed version info in tag $VERSION"

	sleep 2; ./autogen.sh
	rm -rf autom4te.cache $( find -name .svn )

	cd ..
	tar cvzf $PACKAGE-$VERSION.tar.gz \
		--owner=0 --group=0 $PACKAGE-$VERSION
	rm -rf $PACKAGE-$VERSION
	;;
esac

