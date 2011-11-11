# csync2 - cluster synchronization tool, 2nd generation
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
# spec file for package csync2 (Version 2.0)
#

# norootforbuild
# neededforbuild  openssl openssl-devel

BuildRequires: sqlite-devel sqlite librsync gnutls-devel librsync-devel

Name:         csync2
License:      GPL
Group:        System/Monitoring
Requires:     sqlite openssl librsync
Autoreqprov:  on
Version:      2.0
Release:      0.1.rc1
Source0:      csync2-%{version}.tar.gz
URL:          http://oss.linbit.com/csync2
BuildRoot:    %{_tmppath}/%{name}-%{version}-build
Summary:      Cluster sync tool

%description
Csync2 is a cluster synchronization tool. It can be used to keep files on 
multiple hosts in a cluster in sync. Csync2 can handle complex setups with 
much more than just 2 hosts, handle file deletions and can detect conflicts.
It is expedient for HA-clusters, HPC-clusters, COWs and server farms. 



Authors:
--------
    Clifford Wolf <clifford.wolf@linbit.com>

%prep
%setup
%{?suse_update_config:%{suse_update_config}}

%build
export CFLAGS="$RPM_OPT_FLAGS -I/usr/kerberos/include"
if ! [ -f configure ]; then ./autogen.sh; fi
%configure

make all 

%install
[ "$RPM_BUILD_ROOT" != "/" ] && [ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT%{_sbindir}
mkdir -p $RPM_BUILD_ROOT%{_var}/lib/csync2
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/xinetd.d

%makeinstall

install -m 644 csync2.xinetd $RPM_BUILD_ROOT%{_sysconfdir}/xinetd.d/csync2

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && [ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT
make clean

%post
if ! grep -q "^csync2" %{_sysconfdir}/services ; then
     echo "csync2          30865/tcp" >>%{_sysconfdir}/services
fi

%files
%defattr(-,root,root)
%doc ChangeLog README NEWS INSTALL TODO AUTHORS
%{_sbindir}/csync2
%{_sbindir}/csync2-compare
%{_var}/lib/csync2
%{_mandir}/man1/csync2.1.gz
%config(noreplace) %{_sysconfdir}/xinetd.d/csync2
%config(noreplace) %{_sysconfdir}/csync2.cfg

%changelog
* Tue Dec 06 2005 Clifford Wolf <clifford.wolf@linbit.com>
- Some fixes and cleanups for RPM 4.4.1

* Sat Jun 04 2005 Clifford Wolf <clifford.wolf@linbit.com>
- xinetd init script is now "%config(noreplace)"
- Some tiny cleanups

* Mon Dec 10 2004 Tim Jackson <tim@timj.co.uk>
- Added xinetd init script
- Abstracted some config paths
- Tidied

* Tue Oct 12 2004 Clifford Wolf <clifford.wolf@linbit.com>
- Automatic set sepcs file 'Version' tag

* Tue Sep 09 2004 - phil@linbit.com
- initial package
