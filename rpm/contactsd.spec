Name: contactsd
Version: 1.4.1
Release: 1
Summary: Telepathy <> QtContacts bridge for contacts
Group: System/Libraries
URL: https://github.com/nemomobile/contactsd
License: LGPLv2
Source0: %{name}-%{version}.tar.bz2
Requires: systemd
Requires: systemd-user-session-targets
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(Qt5Network)
BuildRequires: pkgconfig(Qt5Test)
BuildRequires: pkgconfig(TelepathyQt5)
BuildRequires: pkgconfig(Qt5Contacts)
BuildRequires: pkgconfig(Qt5Versit)
# mlite required only for tests
BuildRequires: pkgconfig(mlite5)
BuildRequires: pkgconfig(mlocale5)
BuildRequires: pkgconfig(libmkcal-qt5)
BuildRequires: pkgconfig(libkcalcoren-qt5)
BuildRequires: pkgconfig(telepathy-glib)
BuildRequires: pkgconfig(qofono-qt5)
BuildRequires: pkgconfig(qtcontacts-sqlite-qt5-extensions)
BuildRequires: pkgconfig(qt5-boostable)
Requires: mapplauncherd-qt5

%description
contactsd is a service for collecting and observing changes in roster list
from all the users telepathy accounts (buddies, their status and presence
information), and store it to QtContacts.

%files
%defattr(-,root,root,-)
%{_libdir}/systemd/user/contactsd.service
%{_libdir}/systemd/user/user-session.target.wants/contactsd.service
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
export QT_SELECT=5
./configure --bindir %{_bindir} --libdir %{_libdir} --includedir %{_includedir}
%qmake5
make %{?_smp_mflags}


%install
make INSTALL_ROOT=%{buildroot} install

mkdir -p %{buildroot}%{_libdir}/systemd/user/user-session.target.wants  
ln -s ../contactsd.service %{buildroot}%{_libdir}/systemd/user/user-session.target.wants/

%post
if [ "$1" -ge 1 ]; then
systemctl-user daemon-reload || :
systemctl-user restart contactsd.service || :
fi

%postun
if [ "$1" -eq 0 ]; then
systemctl-user stop contactsd.service || :
systemctl-user daemon-reload || :
fi

