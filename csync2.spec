#
# spec file for package csync2
#
# Copyright 2004-2020 LINBIT, Vienna, Austria
#
# SPDX-License-Identifier: GPL-2.0-or-later

Summary:        Cluster synchronization tool
License:        GPL-2.0-or-later
Group:          Productivity/Clustering/HA

Name:           csync2
Version: 2.1
Release: 0rc1
URL:            https://github.com/LINBIT/csync2#readme
Source0:        %{name}-%{version}-%{release}.tar.gz

BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  bison
BuildRequires:  flex
BuildRequires:  libgnutls-devel
BuildRequires:  librsync-devel
BuildRequires:  hostname
# openssl required at build time due to rpmlint checks which run postinstall script which uses openssl
BuildRequires:  openssl
BuildRequires:  pkgconfig
BuildRequires:  sqlite3-devel
Requires:       openssl
Requires:       sqlite3
%if 0%{?suse_version} >= 1210 || 0%{?rhel} >= 7
BuildRequires:  systemd
%endif

BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
Csync2 is a cluster synchronization tool. It can be used to keep files on
multiple hosts in a cluster in sync. Csync2 can handle complex setups with
much more than just 2 hosts, handle file deletions and can detect conflicts.
It is expedient for HA-clusters, HPC-clusters, COWs and server farms.

%prep
%setup
%{?suse_update_config:%{suse_update_config}}

%build
export CFLAGS="$RPM_OPT_FLAGS -I/usr/kerberos/include"
if ! [ -f configure ]; then ./autogen.sh; fi
%configure --enable-mysql --enable-postgres --enable-sqlite3 \
	--sysconfdir=%{_sysconfdir}/csync2 --docdir=%{_docdir}/%{name}

make %{?_smp_mflags}

%install
%makeinstall

mkdir -p %{buildroot}%{_localstatedir}/lib/csync2
install -m 644 doc/csync2.adoc %{buildroot}%{_docdir}/csync2/csync2.adoc
install -m 644 doc/csync2-quickstart.adoc %{buildroot}%{_docdir}/csync2/csync2-quickstart.adoc

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && [ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT
make clean

%pre
%service_add_pre csync2.socket

%post
if ! grep -q "^csync2" %{_sysconfdir}/services ; then
     echo "csync2          30865/tcp" >>%{_sysconfdir}/services
fi
%service_add_post csync2.socket

%files
%defattr(-,root,root)
%{_sbindir}/csync2
%{_sbindir}/csync2-compare
%{_var}/lib/csync2
%doc %{_mandir}/man1/csync2.1.gz
%doc %{_docdir}/csync2/csync2.adoc
%doc %{_docdir}/csync2/ChangeLog
%doc %{_docdir}/csync2/README
%doc %{_docdir}/csync2/AUTHORS
%config(noreplace) %{_sysconfdir}/xinetd.d/csync2
%config(noreplace) %{_sysconfdir}/csync2.cfg

%changelog
* Fri Sep 18 2020 Lars Ellenberg <lars.ellenberg@linbit.com> - 2.1-1
- New upstream release

* Tue Jan 27 2015 Lars Ellenberg <lars.ellenberg@linbit.com> - 2.0-1
- New upstream release
