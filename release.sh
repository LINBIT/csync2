#!/bin/bash
#
# csync2 - cluster synchronization tool, 2nd generation
# Copyright (C) 2004 - 2015 LINBIT Information Technologies GmbH
# http://www.linbit.com; see also AUTHORS
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#
#
# Internal script for tagging a release
# and creating the source tar file.

PACKAGE=csync2
URL=https://github.com/LINBIT/csync2

case "$1" in
  -*)
	echo "Usage: $0 newversion"
	;;
  '')
	git ls-remote -t $URL
	;;
  *)
	VERSION=${1%%-*}
	RELEASE=${1#*-}
	[[ $RELEASE = $VERSION ]] && RELEASE=1
	set -ex

	LANG=C LC_ALL=C date "+csync2 ($VERSION-$RELEASE) unstable; urgency=low%n%n`
		`  * New Upstream Version.%n%n -- Lars Ellenberg `
		`<lars.ellenberg@linbit.com>  %a, %d %b %Y `
		`%H:%M:%S %z%n" > debian/changelog.new
	cat debian/changelog >> debian/changelog.new
	mv debian/changelog.new debian/changelog

	perl -pi -e "s/^AC_INIT.*/AC_INIT(csync2, $VERSION-$RELEASE, csync2\@lists.linbit.com)/" \
		configure.ac
	perl -pi -e "s/^Version:.*/Version: $VERSION/;s/^Release:.*/Release: $RELEASE/" csync2.spec

	# # generate an uptodate copy of the paper
	# git commit -m "Preparing version $VERSION" \
	# 		debian/changelog \
	# 		configure.ac \
	# 		csync2.spec

	# git tag -a -m "$PACKAGE-$VERSION" $PACKAGE-$VERSION

	# include paper.pdf in tarball
	# tar cvzf $PACKAGE-$VERSION.tar.gz \
	# 	--owner=0 --group=0 $PACKAGE-$VERSION
	# rm -rf $PACKAGE-$VERSION
	;;
esac

