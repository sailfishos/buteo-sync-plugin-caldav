TEMPLATE = app
TARGET = tst_reader

QT       -= gui
QT       += network dbus testlib

CONFIG += link_pkgconfig debug console
PKGCONFIG += buteosyncfw5 libkcalcoren-qt5

SOURCES += tst_reader.cpp

INCLUDEPATH += ../../src

LIBS += ../../src/reader.o \
        ../../src/moc_reader.o

OTHER_FILES += data/*xml

datafiles.files += data/*xml
datafiles.path = /opt/tests/buteo/plugins/caldav/data/

target.path = /opt/tests/buteo/plugins/caldav/

INSTALLS += target datafiles

