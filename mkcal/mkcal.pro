TEMPLATE = lib
TARGET = $$qtLibraryTarget(caldavinvitationplugin)
CONFIG += plugin

CONFIG += link_pkgconfig
PKGCONFIG += \
    QmfClient \
    libkcalcoren-qt5 \
    libmkcal-qt5

QT -= gui

HEADERS +=  caldavinvitationplugin.h
SOURCES +=  caldavinvitationplugin.cpp

target.path +=  /${DESTDIR}$$[QT_INSTALL_LIBS]/mkcalplugins/
INSTALLS += target

