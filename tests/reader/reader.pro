TEMPLATE = app
TARGET = tst_reader

QT += testlib

CONFIG += debug

include($$PWD/../../src/src.pri)

HEADERS =
SOURCES = tst_reader.cpp

LIBS += $$PWD/../../src/reader.o \
        $$PWD/../../src/moc_reader.o

OTHER_FILES += data/*xml

datafiles.files += data/*xml
datafiles.path = /opt/tests/buteo/plugins/caldav/data/

target.path = /opt/tests/buteo/plugins/caldav/

INSTALLS += target datafiles

