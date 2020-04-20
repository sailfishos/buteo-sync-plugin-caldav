TEMPLATE = app
TARGET = tst_caldavclient

QT += testlib

CONFIG += debug

include($$PWD/../../src/src.pri)

SOURCES += tst_caldavclient.cpp

target.path = /opt/tests/buteo/plugins/caldav/

INSTALLS += target
