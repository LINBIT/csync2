#
# spec file for package nagios-nrpe (Version 2.0)
#
# Copyright (c) 2004 SUSE LINUX AG, Nuernberg, Germany.
# This file and all modifications and additions to the pristine
# package are under the same license as the package itself.
#
# Please submit bugfixes or comments via http://www.suse.de/feedback/
#

# norootforbuild
# neededforbuild  nagios-plugins openssl openssl-devel

BuildRequires: sqlite-devel sqlite librsync openssl-devel

Name:         csync2
License:      GPL
Group:        System/Monitoring
Requires:     sqlite openssl
Autoreqprov:  on
Version:      SNAPSHOT
Release:      1
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
%setup -n csync2-%{version}
%{?suse_update_config:%{suse_update_config}}

%build
export CFLAGS="$RPM_OPT_FLAGS -I/usr/kerberos/include"
if ! [ -f configure ]; then ./autogen.sh; fi
./configure --prefix=%{_prefix} \

make all 

%install
[ "$RPM_BUILD_ROOT" != "/" ] && [ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/sbin
mkdir -p $RPM_BUILD_ROOT/var/lib/csync2

make install prefix="$RPM_BUILD_ROOT"

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && [ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT
make clean

%post
if ! grep -q "^csync2" /etc/services ; then
     echo "csync2          30865/tcp" >>/etc/services
fi

%files
%defattr(-,root,root)
%doc ChangeLog README NEWS INSTALL TODO AUTHORS
/sbin/csync2
/var/lib/csync2

%changelog -n csync2
* Tue Sep 09 2004 - phil@linbit.com
- initial package
