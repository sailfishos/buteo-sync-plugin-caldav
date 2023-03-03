TEMPLATE = subdirs
SUBDIRS += notebooksyncagent reader propfind caldavclient

tests_xml.path = /opt/tests/buteo/plugins/caldav
tests_xml.files = tests.xml
INSTALLS += tests_xml

OTHER_FILES += tests.xml
