#
# spec file for package csync2 (Version 2.0)
#

# norootforbuild
# neededforbuild  openssl openssl-devel

BuildRequires: sqlite-devel sqlite librsync openssl-devel librsync-devel

Name:         csync2
License:      GPL
Group:        System/Monitoring
Requires:     sqlite openssl librsync
Autoreqprov:  on
Version:      SNAPSHOT
Release:      2
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
%{_var}/lib/csync2
%{_sysconfdir}/xinetd.d/csync2

%changelog -n csync2
* Mon Dec 10 2004 Tim Jackson <tim@timj.co.uk>
- Added xinetd init script
- Abstracted some config paths
- Tidied

* Tue Sep 09 2004 - phil@linbit.com
- initial package
