/* -*- c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2020 Caliste Damien.
 * Contact: Damien Caliste <dcaliste@free.fr>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <QtTest>
#include <QObject>
#include <QFile>

#include <propfind_p.h>

class tst_Propfind : public QObject
{
    Q_OBJECT

public:
    tst_Propfind();
    virtual ~tst_Propfind();

public slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

private slots:
    void parseUserPrincipalResponse_data();
    void parseUserPrincipalResponse();

    void parseUserAddressSetResponse_data();
    void parseUserAddressSetResponse();

    void parseCalendarResponse_data();
    void parseCalendarResponse();

private:
    QNetworkAccessManager *mNAManager;
    Settings mSettings;
    PropFind *mRequest;
};

tst_Propfind::tst_Propfind()
{
}

tst_Propfind::~tst_Propfind()
{
}

void tst_Propfind::initTestCase()
{
    mNAManager = new QNetworkAccessManager;
}

void tst_Propfind::cleanupTestCase()
{
    delete mNAManager;
}

void tst_Propfind::init()
{
    mRequest = new PropFind(mNAManager, &mSettings);
}

void tst_Propfind::cleanup()
{
    delete mRequest;
}

void tst_Propfind::parseUserPrincipalResponse_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<bool>("success");
    QTest::addColumn<QString>("userPrincipal");

    QTest::newRow("empty response")
        << QByteArray()
        << false
        << QString();

    QTest::newRow("invalid response")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:'><D:response><D:href>/</D:href></D:response></D:multistatus>")
        << false
        << QString();

    QTest::newRow("forbidden access")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:'><D:response><D:href>/</D:href><D:propstat><D:prop><D:current-user-principal /></D:prop><D:status>HTTP/1.1 403</D:status></D:propstat></D:response></D:multistatus>")
        << false
        << QString();

    QTest::newRow("valid response")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:'><D:response><D:href>/</D:href><D:propstat><D:prop><D:current-user-principal><D:href>/principals/users/username%40server.tld/</D:href></D:current-user-principal></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << QString::fromLatin1("/principals/users/username%40server.tld/");
}

void tst_Propfind::parseUserPrincipalResponse()
{
    QFETCH(QByteArray, data);
    QFETCH(bool, success);
    QFETCH(QString, userPrincipal);

    QCOMPARE(mRequest->parseUserPrincipalResponse(data), success);
    QCOMPARE(mRequest->userPrincipal(), userPrincipal);
}

void tst_Propfind::parseUserAddressSetResponse_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<bool>("success");
    QTest::addColumn<QString>("userMailtoHref");
    QTest::addColumn<QString>("userHomeHref");

    QTest::newRow("empty response")
        << QByteArray()
        << false
        << QString()
        << QString();

    QTest::newRow("invalid response")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:'><D:response><D:href>/principals/users/username%40server.tld/</D:href></D:response></D:multistatus>")
        << false
        << QString()
        << QString();

    QTest::newRow("forbidden access")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/principals/users/username%40server.tld/</D:href><D:propstat><D:prop><c:calendar-user-address-set /></D:prop><D:status>HTTP/1.1 403</D:status></D:propstat></D:response></D:multistatus>")
        << false
        << QString()
        << QString();

    QTest::newRow("valid mailto")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/principals/users/username%40server.tld/</D:href><D:propstat><D:prop><c:calendar-user-address-set><D:href>mailto:username@server.tld</D:href><D:href>/principals/users/username%40server.tld/</D:href></c:calendar-user-address-set></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << QString::fromLatin1("username@server.tld")
        << QString();

    QTest::newRow("valid home")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/principals/users/username%40server.tld/</D:href><D:propstat><D:prop><c:calendar-home-set><D:href>/caldav/</D:href></c:calendar-home-set></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat><D:propstat><D:prop><c:calendar-user-address-set /></D:prop><D:status>HTTP/1.1 404</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << QString()
        << QString("/caldav/");
}

void tst_Propfind::parseUserAddressSetResponse()
{
    QFETCH(QByteArray, data);
    QFETCH(bool, success);
    QFETCH(QString, userMailtoHref);

    QCOMPARE(mRequest->parseUserAddressSetResponse(data), success);
    QCOMPARE(mRequest->userAddressSets()[QStringLiteral("caldav")].mailto, userMailtoHref);
}

Q_DECLARE_METATYPE(Buteo::Dav::CalendarInfo)
void tst_Propfind::parseCalendarResponse_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<bool>("success");
    QTest::addColumn<QList<Buteo::Dav::CalendarInfo>>("calendars");

    QTest::newRow("empty response")
        << QByteArray()
        << false
        << QList<Buteo::Dav::CalendarInfo>();

    QTest::newRow("invalid response")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:'><D:response><D:href>/calendars/0/</D:href></D:response></D:multistatus>")
        << false
        << QList<Buteo::Dav::CalendarInfo>();

    QTest::newRow("forbidden access")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/calendars/0/</D:href><D:propstat><D:prop><D:displayname /><calendar-color xmlns=\"http://apple.com/ns/ical/\" /><D:resourcetype /><D:current-user-principal /></D:prop><D:status>HTTP/1.1 403</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << QList<Buteo::Dav::CalendarInfo>();

    QTest::newRow("not a calendar")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/calendars/</D:href><D:propstat><D:prop><D:resourcetype><D:collection /></D:resourcetype><D:current-user-principal><D:href>/principals/users/username%40server.tld/</D:href></D:current-user-principal></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << QList<Buteo::Dav::CalendarInfo>();

    QTest::newRow("one valid calendar")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/calendars/0/</D:href><D:propstat><D:prop><D:displayname>Calendar 0</D:displayname><calendar-color xmlns=\"http://apple.com/ns/ical/\">#FF0000</calendar-color><D:resourcetype><c:calendar /><D:collection /></D:resourcetype><D:current-user-principal><D:href>/principals/users/username%40server.tld/</D:href></D:current-user-principal><D:current-user-privilege-set><D:privilege><D:read /></D:privilege><D:privilege><D:write /></D:privilege></D:current-user-privilege-set></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << (QList<Buteo::Dav::CalendarInfo>() << Buteo::Dav::CalendarInfo{
                QString::fromLatin1("/calendars/0/"),
                    QString::fromLatin1("Calendar 0"),
                    QString(),
                    QString::fromLatin1("#FF0000"),
                    QString::fromLatin1("/principals/users/username%40server.tld/")});

    QTest::newRow("one read-only calendar")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/calendars/0/</D:href><D:propstat><D:prop><D:displayname>Calendar 0</D:displayname><calendar-color xmlns=\"http://apple.com/ns/ical/\">#FF0000</calendar-color><D:resourcetype><c:calendar /><D:collection /></D:resourcetype><D:current-user-principal><D:href>/principals/users/username%40server.tld/</D:href></D:current-user-principal><D:current-user-privilege-set><D:privilege><D:read /></D:privilege></D:current-user-privilege-set></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << (QList<Buteo::Dav::CalendarInfo>() << Buteo::Dav::CalendarInfo{
                QString::fromLatin1("/calendars/0/"),
                    QString::fromLatin1("Calendar 0"),
                    QString(),
                    QString::fromLatin1("#FF0000"),
                    QString::fromLatin1("/principals/users/username%40server.tld/"),
                    Buteo::Dav::READ});

    QTest::newRow("missing current-user-principal")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/calendars/0/</D:href><D:propstat><D:prop><D:displayname>Calendar 0</D:displayname><calendar-color xmlns=\"http://apple.com/ns/ical/\">#FF0000</calendar-color><D:resourcetype><c:calendar /><D:collection /></D:resourcetype></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat><D:propstat><D:prop><D:current-user-principal /></D:prop><D:status>HTTP/1.1 404</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << (QList<Buteo::Dav::CalendarInfo>() << Buteo::Dav::CalendarInfo{
                QString::fromLatin1("/calendars/0/"),
                    QString::fromLatin1("Calendar 0"),
                    QString(),
                    QString::fromLatin1("#FF0000"),
                    QString()});

    QTest::newRow("missing displayname")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/calendars/0/</D:href><D:propstat><D:prop><calendar-color xmlns=\"http://apple.com/ns/ical/\">#FF0000</calendar-color><D:resourcetype><c:calendar /><D:collection /></D:resourcetype><D:current-user-principal><D:href>/principals/users/username%40server.tld/</D:href></D:current-user-principal></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat><D:propstat><D:prop><D:displayname /></D:prop><D:status>HTTP/1.1 404</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << (QList<Buteo::Dav::CalendarInfo>() << Buteo::Dav::CalendarInfo{
                QString::fromLatin1("/calendars/0/"),
                    QString::fromLatin1("Calendar"),
                    QString(),
                    QString::fromLatin1("#FF0000"),
                    QString::fromLatin1("/principals/users/username%40server.tld/")});

    QTest::newRow("missing privileges")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/calendars/0/</D:href><D:propstat><D:prop><D:displayname>Calendar 0</D:displayname><calendar-color xmlns=\"http://apple.com/ns/ical/\">#FF0000</calendar-color><D:resourcetype><c:calendar /><D:collection /></D:resourcetype><D:current-user-principal><D:href>/principals/users/username%40server.tld/</D:href></D:current-user-principal></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat><D:propstat><D:prop><D:current-user-privilege-set /></D:prop><D:status>HTTP/1.1 404</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << (QList<Buteo::Dav::CalendarInfo>() << Buteo::Dav::CalendarInfo{
                QString::fromLatin1("/calendars/0/"),
                    QString::fromLatin1("Calendar 0"),
                    QString(),
                    QString::fromLatin1("#FF0000"),
                    QString::fromLatin1("/principals/users/username%40server.tld/")});

    QTest::newRow("two valid calendars")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/calendars/0/</D:href><D:propstat><D:prop><D:displayname>Calendar 0</D:displayname><calendar-color xmlns=\"http://apple.com/ns/ical/\">#FF0000</calendar-color><D:resourcetype><c:calendar /><D:collection /></D:resourcetype><D:current-user-principal><D:href>/principals/users/username%40server.tld/</D:href></D:current-user-principal></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response><D:response><D:href>/calendars/1/</D:href><D:propstat><D:prop><D:displayname>Calendar 1</D:displayname><calendar-color xmlns=\"http://apple.com/ns/ical/\">#FFFF00</calendar-color><D:resourcetype><c:calendar /><D:collection /></D:resourcetype><D:current-user-principal><D:href>/principals/users/username%40server.tld/</D:href></D:current-user-principal></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << (QList<Buteo::Dav::CalendarInfo>() << Buteo::Dav::CalendarInfo{
                QString::fromLatin1("/calendars/0/"),
                    QString::fromLatin1("Calendar 0"),
                    QString(),
                    QString::fromLatin1("#FF0000"),
                    QString::fromLatin1("/principals/users/username%40server.tld/")}
            << Buteo::Dav::CalendarInfo{
                QString::fromLatin1("/calendars/1/"),
                    QString::fromLatin1("Calendar 1"),
                    QString(),
                    QString::fromLatin1("#FFFF00"),
                    QString::fromLatin1("/principals/users/username%40server.tld/")});

    Buteo::Dav::CalendarInfo todos(QString::fromLatin1("/calendars/0/"),
                                   QString::fromLatin1("Calendar 0"),
                                   QString(),
                                   QString::fromLatin1("#FF0000"),
                                   QString::fromLatin1("/principals/users/username%40server.tld/"));
    todos.allowEvents = false;
    todos.allowTodos = true;
    todos.allowJournals = false;
    QTest::newRow("one valid task manager")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/calendars/0/</D:href><D:propstat><D:prop><D:displayname>Calendar 0</D:displayname><calendar-color xmlns=\"http://apple.com/ns/ical/\">#FF0000</calendar-color><D:resourcetype><c:calendar /><D:collection /></D:resourcetype><D:current-user-principal><D:href>/principals/users/username%40server.tld/</D:href></D:current-user-principal><D:current-user-privilege-set><D:privilege><D:read /></D:privilege><D:privilege><D:write /></D:privilege></D:current-user-privilege-set><c:supported-calendar-component-set><c:comp name=\"VTODO\" /></c:supported-calendar-component-set></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << (QList<Buteo::Dav::CalendarInfo>() << todos);

    QTest::newRow("missing component set")
        << QByteArray("<?xml version='1.0' encoding='utf-8'?><D:multistatus xmlns:D='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><D:response><D:href>/calendars/0/</D:href><D:propstat><D:prop><calendar-color xmlns=\"http://apple.com/ns/ical/\">#FF0000</calendar-color><D:resourcetype><c:calendar /><D:collection /></D:resourcetype><D:current-user-principal><D:href>/principals/users/username%40server.tld/</D:href></D:current-user-principal></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat><D:propstat><D:prop><c:supported-calendar-component-set /></D:prop><D:status>HTTP/1.1 404</D:status></D:propstat></D:response></D:multistatus>")
        << true
        << (QList<Buteo::Dav::CalendarInfo>() << Buteo::Dav::CalendarInfo{
                QString::fromLatin1("/calendars/0/"),
                    QString::fromLatin1("Calendar"),
                    QString(),
                    QString::fromLatin1("#FF0000"),
                    QString::fromLatin1("/principals/users/username%40server.tld/")});
}

void tst_Propfind::parseCalendarResponse()
{
    QFETCH(QByteArray, data);
    QFETCH(bool, success);
    QFETCH(QList<Buteo::Dav::CalendarInfo>, calendars);

    QCOMPARE(mRequest->parseCalendarResponse(data), success);
    const QList<Buteo::Dav::CalendarInfo> response = mRequest->calendars();
    QCOMPARE(response, calendars);
}

#include "tst_propfind.moc"
QTEST_MAIN(tst_Propfind)
