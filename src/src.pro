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

!contains (DEFINES, BUTEO_OUT_OF_PROCESS_SUPPORT) {
    TEMPLATE = lib
    CONFIG += plugin
    target.path = /usr/lib/buteo-plugins-qt5
}

contains (DEFINES, BUTEO_OUT_OF_PROCESS_SUPPORT) {
    TEMPLATE = app
    target.path = /usr/lib/buteo-plugins-qt5/oopp
    DEFINES += CLIENT_PLUGIN
    DEFINES += "CLASSNAME=CalDavClient"
    DEFINES += CLASSNAME_H=\\\"caldavclient.h\\\"
    INCLUDE_DIR = $$system(pkg-config --cflags buteosyncfw5|cut -f2 -d'I')

    HEADERS += $$INCLUDE_DIR/ButeoPluginIfaceAdaptor.h   \
               $$INCLUDE_DIR/PluginCbImpl.h              \
               $$INCLUDE_DIR/PluginServiceObj.h

    SOURCES += $$INCLUDE_DIR/ButeoPluginIfaceAdaptor.cpp \
               $$INCLUDE_DIR/PluginCbImpl.cpp            \
               $$INCLUDE_DIR/PluginServiceObj.cpp        \
               $$INCLUDE_DIR/plugin_main.cpp
}

sync.path = /etc/buteo/profiles/sync
sync.files = xmls/sync/*

client.path = /etc/buteo/profiles/client
client.files = xmls/client/*

INSTALLS += target sync client
