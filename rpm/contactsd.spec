Name: contactsd
Version: 1.3.0
Release: 1
Summary: Telepathy <> tracker bridge for contacts
Group: System/Libraries
URL: https://github.com/nemomobile/contactsd
License: LGPLv2
Source0: %{name}-%{version}.tar.bz2
Source1: contactsd.desktop
Source2: contactsd.service
BuildRequires: pkgconfig(QtCore)
BuildRequires: pkgconfig(QtSparql)
BuildRequires: pkgconfig(QtSparqlTrackerExtensions)
BuildRequires: pkgconfig(tracker-sparql-0.14)
BuildRequires: pkgconfig(TelepathyQt4)
BuildRequires: pkgconfig(QtContacts)
BuildRequires: pkgconfig(cubi-0.1)
# mlite required only for tests
BuildRequires: pkgconfig(mlite)
BuildRequires: pkgconfig(mlocale)
BuildRequires: pkgconfig(libmkcal)
BuildRequires: pkgconfig(telepathy-glib)
BuildRequires: libcubi-tracker-ontologies-devel
BuildRequires: libqtcontacts-tracker-extensions-devel

%description
contactsd is a service for collecting and observing changes in roster list
from all the users telepathy accounts (buddies, their status and presence
information), and store it to tracker.

%files
%defattr(-,root,root,-)
%config %{_sysconfdir}/xdg/autostart/contactsd.desktop
%{_libdir}/systemd/user/contactsd.service
%{_bindir}/contactsd
%{_libdir}/contactsd-1.0/plugins/*.so
# we currently don't have a backup framework
%exclude /usr/share/backup-framework/applications/contactsd.conf


%package devel
Summary: Development files for %{name}
Group: Development/Libraries
Requires: %{name} = %{version}-%{release}

%description devel
%{summary}.

%files devel
%defattr(-,root,root,-)
%{_includedir}/contactsd-1.0/*
%dir %{_includedir}/contactsd-1.0
%{_libdir}/pkgconfig/*.pc


%package tests
Summary: Tests for %{name}
Group: Development/Libraries
Requires: %{name} = %{version}-%{release}

%description tests
%{summary}.

%files tests
%defattr(-,root,root,-)
/opt/tests/%{name}


%prep
%setup -q -n %{name}-%{version}

%build
./configure --bindir %{_bindir} --libdir %{_libdir} --includedir %{_includedir}
make %{?_smp_mflags}


%install
make INSTALL_ROOT=%{buildroot} install
mkdir -p %{buildroot}/%{_sysconfdir}/xdg/autostart
cp %{SOURCE1} %{buildroot}/%{_sysconfdir}/xdg/autostart/
mkdir -p %{buildroot}/%{_libdir}/systemd/user/
cp %{SOURCE2} %{buildroot}/%{_libdir}/systemd/user/

