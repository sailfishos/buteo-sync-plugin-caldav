TARGET = caldav-client

include(src.pri)

VER_MAJ = 0
VER_MIN = 1
VER_PAT = 0

QMAKE_CXXFLAGS += -Wall \
    -g \
    -Wno-cast-align \
    -O2 -finline-functions

DEFINES += BUTEOCALDAVPLUGIN_LIBRARY

TEMPLATE = lib
CONFIG += plugin
target.path = $$[QT_INSTALL_LIBS]/buteo-plugins-qt5/oopp

sync.path = /etc/buteo/profiles/sync
sync.files = xmls/sync/*

client.path = /etc/buteo/profiles/client
client.files = xmls/client/*

INSTALLS += target sync client
