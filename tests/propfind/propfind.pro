TEMPLATE = app
TARGET = tst_propfind

QT += testlib network
QT -= gui

CONFIG += debug

INCLUDEPATH += ../../lib

SOURCES += tst_propfind.cpp \
    ../../lib/propfind.cpp \
    ../../lib/request.cpp \
    ../../lib/logging.cpp \
    ../../lib/settings.cpp

HEADERS += ../../lib/propfind_p.h \
    ../../lib/request_p.h

target.path = /opt/tests/buteo/plugins/caldav/

INSTALLS += target
