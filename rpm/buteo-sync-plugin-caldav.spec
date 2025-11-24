Name:       buteo-sync-plugin-caldav
Summary:    Syncs calendar data from CalDAV services
Version:    0.4.0
Release:    1
License:    LGPLv2
URL:        https://github.com/sailfishos/buteo-sync-plugin-caldav/
Source0:    %{name}-%{version}.tar.bz2
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Test)
BuildRequires:  pkgconfig(libsignon-qt5)
BuildRequires:  pkgconfig(libsailfishkeyprovider)
BuildRequires:  pkgconfig(libmkcal-qt5) >= 0.7.17
BuildRequires:  pkgconfig(KF5CalendarCore) >= 5.79
BuildRequires:  pkgconfig(buteosyncfw5) >= 0.10.11
BuildRequires:  pkgconfig(accounts-qt5)
BuildRequires:  pkgconfig(signon-oauth2plugin)
BuildRequires:  pkgconfig(QmfClient)
Requires: buteo-syncfw-qt5-msyncd

%description
A Buteo plugin which syncs calendar data from CalDAV services

%package devel
Summary: development files for the DAV library
Requires: %{name} = %{version}-%{release}
%description devel
This package contains development files to link with the DAV library.

%package tools
Summary: command line interface to perform DAV operations
Requires: %{name} = %{version}-%{release}
%description tools
This package contains a command-line tool to perform DAV queries.

%package tests
Summary: Unit tests for buteo-sync-plugin-caldav
Requires: blts-tools
Requires: %{name} = %{version}-%{release}
%description tests
This package contains unit tests for the CalDAV Buteo sync plugin.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
%make_build

%install
%qmake5_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%license LICENSE
%config %{_sysconfdir}/buteo/profiles/client/caldav.xml
%config %{_sysconfdir}/buteo/profiles/sync/caldav-sync.xml
%{_libdir}/libbuteodav.so.*
%{_libdir}/buteo-plugins-qt5/oopp/libcaldav-client.so
#mkcal invitation plugin
%{_libdir}/mkcalplugins/libcaldavinvitationplugin.so

%files tools
%{_bindir}/dav-client

%files devel
%{_libdir}/libbuteodav.so
%{_includedir}/buteodav/*.h
%{_libdir}/pkgconfig/libbuteodav.pc

%files tests
/opt/tests/buteo/plugins/caldav/tests.xml
/opt/tests/buteo/plugins/caldav/tst_reader
/opt/tests/buteo/plugins/caldav/tst_notebooksyncagent
/opt/tests/buteo/plugins/caldav/tst_propfind
/opt/tests/buteo/plugins/caldav/tst_caldavclient
/opt/tests/buteo/plugins/caldav/data/notebooksyncagent_insert_exdate.xml
/opt/tests/buteo/plugins/caldav/data/notebooksyncagent_insert_and_update.xml
/opt/tests/buteo/plugins/caldav/data/notebooksyncagent_recurring.xml
/opt/tests/buteo/plugins/caldav/data/notebooksyncagent_simple.xml
/opt/tests/buteo/plugins/caldav/data/notebooksyncagent_orphanexceptions.xml
/opt/tests/buteo/plugins/caldav/data/reader_CR_description.xml
/opt/tests/buteo/plugins/caldav/data/reader_UID.xml
/opt/tests/buteo/plugins/caldav/data/reader_earlyUID.xml
/opt/tests/buteo/plugins/caldav/data/reader_UTF8_description.xml
/opt/tests/buteo/plugins/caldav/data/reader_base.xml
/opt/tests/buteo/plugins/caldav/data/reader_noevent.xml
/opt/tests/buteo/plugins/caldav/data/reader_missing.xml
/opt/tests/buteo/plugins/caldav/data/reader_basic_vcal.xml
/opt/tests/buteo/plugins/caldav/data/reader_noxml.xml
/opt/tests/buteo/plugins/caldav/data/reader_nodav.xml
/opt/tests/buteo/plugins/caldav/data/reader_fullday.xml
/opt/tests/buteo/plugins/caldav/data/reader_fullday_vcal.xml
/opt/tests/buteo/plugins/caldav/data/reader_xmltag.xml
/opt/tests/buteo/plugins/caldav/data/reader_urldescription.xml
/opt/tests/buteo/plugins/caldav/data/reader_relativealarm.xml
/opt/tests/buteo/plugins/caldav/data/reader_cdata.xml
/opt/tests/buteo/plugins/caldav/data/reader_todo_pending.xml
/opt/tests/buteo/plugins/caldav/data/reader_unexpected_elements.xml

