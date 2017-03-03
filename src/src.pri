QT -= gui
QT += network dbus

CONFIG += link_pkgconfig console

PKGCONFIG += buteosyncfw5 libsignon-qt5 accounts-qt5 libsailfishkeyprovider
PKGCONFIG += signon-oauth2plugin libkcalcoren-qt5 libmkcal-qt5

INCLUDEPATH += $$PWD

SOURCES += \
        $$PWD/caldavclient.cpp \
        $$PWD/report.cpp \
        $$PWD/put.cpp \
        $$PWD/delete.cpp \
        $$PWD/reader.cpp \
        $$PWD/settings.cpp \
        $$PWD/request.cpp \
        $$PWD/authhandler.cpp \
        $$PWD/incidencehandler.cpp \
        $$PWD/notebooksyncagent.cpp \
        $$PWD/semaphore_p.cpp

HEADERS += \
        $$PWD/caldavclient.h \
        $$PWD/buteo-caldav-plugin.h \
        $$PWD/report.h \
        $$PWD/put.h \
        $$PWD/delete.h \
        $$PWD/reader.h \
        $$PWD/settings.h \
        $$PWD/request.h \
        $$PWD/authhandler.h \
        $$PWD/incidencehandler.h \
        $$PWD/notebooksyncagent.h \
        $$PWD/semaphore_p.h

OTHER_FILES += \
            $$PWD/xmls/client/caldav.xml \
            $$PWD/xmls/sync/caldav-sync.xml
