#!/bin/sh
#
#  Example /root/.bash_logout file for csync2.
#
#  csync2 - cluster synchronization tool, 2nd generation
#  Copyright (C) 2004 - 2013 LINBIT Information Technologies GmbH
#  http://www.linbit.com; see also AUTHORS
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

csync2 -cr /
if csync2 -M; then
	echo "!!"
	echo "!! There are unsynced changes! Type 'yes' if you still want to"
	echo "!! exit (or press crtl-c) and anything else if you want to start"
	echo "!! a new login shell instead."
	echo "!!"
	if read -p "Do you really want to logout? " in &&
	   [ ".$in" != ".yes" ]; then
		exec bash --login
	fi
fi

