QT -= gui
QT += network dbus

CONFIG += link_pkgconfig console

PKGCONFIG += buteosyncfw5 libsignon-qt5 accounts-qt5 libsailfishkeyprovider
PKGCONFIG += signon-oauth2plugin KF5CalendarCore libmkcal-qt5

INCLUDEPATH += $$PWD $$PWD/../lib
LIBS += -L$$PWD/../lib -lbuteodav

SOURCES += \
        $$PWD/caldavclient.cpp \
        $$PWD/authhandler.cpp \
        $$PWD/incidencehandler.cpp \
        $$PWD/notebooksyncagent.cpp \
        $$PWD/logging.cpp

HEADERS += \
        $$PWD/caldavclient.h \
        $$PWD/buteo-caldav-plugin.h \
        $$PWD/authhandler.h \
        $$PWD/incidencehandler.h \
        $$PWD/notebooksyncagent.h \
        $$PWD/logging.h

OTHER_FILES += \
        $$PWD/xmls/client/caldav.xml \
        $$PWD/xmls/sync/caldav-sync.xml

MOC_DIR=$$PWD/.moc/
OBJECTS_DIR=$$PWD/.obj/
