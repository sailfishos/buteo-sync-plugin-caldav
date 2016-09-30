TEMPLATE = app
TARGET = tst_notebooksyncagent

QT       -= gui
QT       += network dbus testlib

CONFIG += link_pkgconfig debug console
PKGCONFIG += buteosyncfw5 libkcalcoren-qt5 libmkcal-qt5

SOURCES += tst_notebooksyncagent.cpp

INCLUDEPATH += ../../src

LIBS += ../../src/notebooksyncagent.o \
        ../../src/moc_notebooksyncagent.o \
        ../../src/report.o \
        ../../src/moc_report.o \
        ../../src/request.o \
        ../../src/moc_request.o \
        ../../src/delete.o \
        ../../src/moc_delete.o \
        ../../src/put.o \
        ../../src/moc_put.o \
        ../../src/settings.o \
        ../../src/incidencehandler.o \
        ../../src/reader.o \
        ../../src/moc_reader.o

target.path = /opt/tests/buteo/plugins/caldav/
INSTALLS += target
