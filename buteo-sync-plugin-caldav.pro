TEMPLATE = subdirs

SUBDIRS = src tests mkcal
tests.depends = src

OTHER_FILES += rpm/buteo-sync-plugin-caldav.spec \
            src/xmls/client/caldav.xml \
            src/xmls/sync/caldav-sync.xml
