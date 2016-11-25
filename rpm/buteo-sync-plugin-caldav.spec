Name:       buteo-sync-plugin-caldav
Summary:    Syncs calendar data from CalDAV services
Version:    0.1.38
Release:    1
Group:      System/Libraries
License:    LGPLv2.1
URL:        https://github.com/nemomobile/buteo-sync-plugin-caldav
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(mlite5)
BuildRequires:  pkgconfig(libsignon-qt5)
BuildRequires:  pkgconfig(libsailfishkeyprovider)
BuildRequires:  pkgconfig(libmkcal-qt5)
BuildRequires:  pkgconfig(libkcalcoren-qt5)
BuildRequires:  pkgconfig(buteosyncfw5)
BuildRequires:  pkgconfig(accounts-qt5)
BuildRequires:  pkgconfig(signon-oauth2plugin)
Requires: buteo-syncfw-qt5-msyncd
Requires: mkcal-qt5

%description
A Buteo plugin which syncs calendar data from CalDAV services

%package tests
Summary: Unit tests for buteo-sync-plugin-caldav
Group: System/Libraries
BuildRequires: pkgconfig(Qt5Test)
Requires: blts-tools
Requires: %{name} = %{version}-%{release}
%description tests
This package contains unit tests for the CalDAV Buteo sync plugin.

%files
%defattr(-,root,root,-)
#out-of-process-plugin
/usr/lib/buteo-plugins-qt5/oopp/caldav-client
#in-process-plugin
#/usr/lib/buteo-plugins-qt5/libcaldav-client.so
%config %{_sysconfdir}/buteo/profiles/client/caldav.xml
%config %{_sysconfdir}/buteo/profiles/sync/caldav-sync.xml

%files tests
%defattr(-,root,root,-)
/opt/tests/buteo/plugins/caldav/tst_reader
/opt/tests/buteo/plugins/caldav/tst_notebooksyncagent
/opt/tests/buteo/plugins/caldav/data/notebooksyncagent_insert_and_update.xml
/opt/tests/buteo/plugins/caldav/data/notebooksyncagent_recurring.xml
/opt/tests/buteo/plugins/caldav/data/notebooksyncagent_simple.xml
/opt/tests/buteo/plugins/caldav/data/reader_CR_description.xml
/opt/tests/buteo/plugins/caldav/data/reader_UID.xml
/opt/tests/buteo/plugins/caldav/data/reader_earlyUID.xml
/opt/tests/buteo/plugins/caldav/data/reader_UTF8_description.xml
/opt/tests/buteo/plugins/caldav/data/reader_base.xml
/opt/tests/buteo/plugins/caldav/data/reader_noevent.xml
/opt/tests/buteo/plugins/caldav/data/reader_missing.xml
/opt/tests/buteo/plugins/caldav/data/reader_basic_vcal.xml

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 "DEFINES+=BUTEO_OUT_OF_PROCESS_SUPPORT"
make %{?jobs:-j%jobs}

%pre
rm -f /home/nemo/.cache/msyncd/sync/client/caldav.xml
rm -f /home/nemo/.cache/msyncd/sync/caldav-sync.xml

%install
rm -rf %{buildroot}
%qmake5_install

%post
systemctl-user try-restart msyncd.service || :
