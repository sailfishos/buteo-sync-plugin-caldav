TEMPLATE = app
TARGET = dav-client
QT -= gui
QT += network

CONFIG += console

INCLUDEPATH += ../lib
LIBS += -L../lib -lbuteodav

SOURCES += dav-client.cpp

target.path = $$INSTALL_ROOT/usr/bin/
INSTALLS += target
