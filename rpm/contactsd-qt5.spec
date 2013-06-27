Name: contactsd-qt5
Version: 1.4.1
Release: 1
Summary: Telepathy <> QtContacts bridge for contacts
Group: System/Libraries
URL: https://github.com/nemomobile/contactsd
License: LGPLv2
Source0: %{name}-%{version}.tar.bz2
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(TelepathyQt5)
BuildRequires: pkgconfig(Qt5Contacts)
# mlite required only for tests
BuildRequires: pkgconfig(mlite5)
BuildRequires: pkgconfig(mlocale5)
BuildRequires: pkgconfig(libmkcal-qt5)
BuildRequires: pkgconfig(libkcalcoren-qt5)
BuildRequires: pkgconfig(telepathy-glib)
Obsoletes: contactsd < %{version}
Provides: contactsd = %{version}

%description
contactsd is a service for collecting and observing changes in roster list
from all the users telepathy accounts (buddies, their status and presence
information), and store it to QtContacts.

%files
%defattr(-,root,root,-)
%{_libdir}/systemd/user/contactsd-qt5.service
%{_bindir}/contactsd-qt5
%{_libdir}/contactsd-qt5-1.0/plugins/*.so
# we currently don't have a backup framework
%exclude /usr/share/backup-framework/applications/contactsd-qt5.conf


%package devel
Summary: Development files for %{name}
Group: Development/Libraries
Requires: %{name} = %{version}-%{release}

%description devel
%{summary}.

%files devel
%defattr(-,root,root,-)
%{_includedir}/contactsd-qt5-1.0/*
%dir %{_includedir}/contactsd-qt5-1.0
%{_libdir}/pkgconfig/*.pc


%package tests
Summary: Tests for %{name}
Group: Development/Libraries
Requires: %{name} = %{version}-%{release}
Obsoletes: contactsd-tests

%description tests
%{summary}.

%files tests
%defattr(-,root,root,-)
/opt/tests/%{name}


%prep
%setup -q -n %{name}-%{version}

%build
export QT_SELECT=5
./configure --bindir %{_bindir} --libdir %{_libdir} --includedir %{_includedir}
%qmake5
make %{?_smp_mflags}


%install
make INSTALL_ROOT=%{buildroot} install
