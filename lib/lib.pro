TEMPLATE = lib
TARGET = buteodav
QT -= gui
QT += network
CONFIG += qt hide_symbols create_prl create_pc no_install_prl

SOURCES += report.cpp \
        head.cpp \
        put.cpp \
        delete.cpp \
        propfind.cpp \
        request.cpp \
        settings.cpp \
        davclient.cpp \
        reader.cpp \
        logging.cpp

PUBLIC_HEADERS += davtypes.h \
        davclient.h \
        davexport.h

HEADERS += $$PUBLIC_HEADERS \
        report_p.h \
        head_p.h \
        put_p.h \
        delete_p.h \
        propfind_p.h \
        request_p.h \
        settings_p.h \
        reader_p.h \
        logging_p.h

target.path = $$[QT_INSTALL_LIBS]
pkgconfig.files = lib$$TARGET.pc
pkgconfig.path = $$target.path/pkgconfig
headers.files = $$PUBLIC_HEADERS
headers.path = /usr/include/$$TARGET

QMAKE_PKGCONFIG_NAME = lib$$TARGET
QMAKE_PKGCONFIG_FILE = lib$$TARGET
QMAKE_PKGCONFIG_DESCRIPTION = A library to handle DAV operations
QMAKE_PKGCONFIG_LIBDIR = $$target.path
QMAKE_PKGCONFIG_INCDIR = $$headers.path
QMAKE_PKGCONFIG_DESTDIR = pkgconfig
QMAKE_PKGCONFIG_VERSION = $$VERSION
QMAKE_PKGCONFIG_REQUIRES = Qt5Core Qt5Network

INSTALLS += target headers pkgconfig
