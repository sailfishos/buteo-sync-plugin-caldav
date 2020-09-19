TEMPLATE = app
TARGET = tst_incidencehandler

QT += testlib
QT -= gui

CONFIG += debug

include($$PWD/../../src/src.pri)

SOURCES += tst_incidencehandler.cpp

target.path = /opt/tests/buteo/plugins/caldav/

INSTALLS += target
