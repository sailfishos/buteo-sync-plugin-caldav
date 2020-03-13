TEMPLATE = app
TARGET = tst_propfind

QT += testlib

CONFIG += debug

include($$PWD/../../src/src.pri)

SOURCES += tst_propfind.cpp

target.path = /opt/tests/buteo/plugins/caldav/

INSTALLS += target
