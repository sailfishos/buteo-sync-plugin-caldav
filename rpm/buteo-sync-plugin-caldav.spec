Name:       buteo-sync-plugin-caldav
Summary:    Syncs calendar data from CalDAV services
Version:    0.1.72
Release:    1
License:    LGPLv2
URL:        https://git.sailfishos.org/mer-core/buteo-sync-plugin-caldav
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(libsignon-qt5)
BuildRequires:  pkgconfig(libsailfishkeyprovider)
BuildRequires:  pkgconfig(libmkcal-qt5) >= 0.5.20
BuildRequires:  kcalcore-qt5-devel >= 4.10.2+9git27
BuildRequires:  pkgconfig(buteosyncfw5)
BuildRequires:  pkgconfig(accounts-qt5)
BuildRequires:  pkgconfig(signon-oauth2plugin)
BuildRequires:  pkgconfig(QmfClient)
Requires: buteo-syncfw-qt5-msyncd

%description
A Buteo plugin which syncs calendar data from CalDAV services

%package tests
Summary: Unit tests for buteo-sync-plugin-caldav
BuildRequires: pkgconfig(Qt5Test)
Requires: %{name} = %{version}-%{release}
%description tests
This package contains unit tests for the CalDAV Buteo sync plugin.

%files
%defattr(-,root,root,-)
%config %{_sysconfdir}/buteo/profiles/client/caldav.xml
%config %{_sysconfdir}/buteo/profiles/sync/caldav-sync.xml
#out-of-process-plugin
%{_libdir}/buteo-plugins-qt5/oopp/caldav-client
#in-process-plugin
#%{_libdir}/buteo-plugins-qt5/libcaldav-client.so
#mkcal invitation plugin
%{_libdir}/mkcalplugins/libcaldavinvitationplugin.so

%files tests
%defattr(-,root,root,-)
/opt/tests/buteo/plugins/caldav/tst_reader
/opt/tests/buteo/plugins/caldav/tst_notebooksyncagent
/opt/tests/buteo/plugins/caldav/tst_incidencehandler
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

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5 "DEFINES+=BUTEO_OUT_OF_PROCESS_SUPPORT"
make %{?_smp_mflags}

%pre
# remove legacy files
rm -f /home/nemo/.cache/msyncd/sync/client/caldav.xml || :
rm -f /home/nemo/.cache/msyncd/sync/caldav-sync.xml || :

%install
rm -rf %{buildroot}
%qmake5_install

%post
systemctl-user try-restart msyncd.service || :
