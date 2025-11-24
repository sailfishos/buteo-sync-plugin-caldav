TEMPLATE = app
TARGET = tst_reader

QT += testlib
QT -= gui

CONFIG += debug

INCLUDEPATH += ../../lib
LIBS += -L../../lib -lbuteodav

include($$PWD/../../src/src.pri)

SOURCES += tst_reader.cpp

OTHER_FILES += data/*xml

datafiles.files += data/*xml
datafiles.path = /opt/tests/buteo/plugins/caldav/data/

target.path = /opt/tests/buteo/plugins/caldav/

INSTALLS += target datafiles
