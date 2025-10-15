TEMPLATE = subdirs

SUBDIRS = lib src tests mkcal tools
src.depends = lib
tests.depends = src
tools.depends = lib

OTHER_FILES += rpm/buteo-sync-plugin-caldav.spec \
            src/xmls/client/caldav.xml \
            src/xmls/sync/caldav-sync.xml
