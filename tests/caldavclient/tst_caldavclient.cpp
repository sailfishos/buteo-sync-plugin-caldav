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

#include "caldavclient.h"

#include <ProfileEngineDefs.h>
#include <Accounts/AccountService>

class tst_CalDavClient : public QObject
{
    Q_OBJECT

public:
    tst_CalDavClient();
    virtual ~tst_CalDavClient();

public slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

private slots:
    void initConfig();
    void initConfigWithSettingsInAccount();
    void addInitCalendars();
    void loadAccountCalendars();
    void mergeAccountCalendars();
    void removeAccountCalendar();

private:
    Accounts::Manager* mManager;
    Accounts::Account* mAccount;
    Buteo::SyncProfile mProfile;
};

tst_CalDavClient::tst_CalDavClient()
    : mManager(nullptr)
    , mAccount(nullptr)
    , mProfile(QLatin1String("test_profile"))
{
}

tst_CalDavClient::~tst_CalDavClient()
{
}

static const QString SERVER_ADDRESS = QLatin1String("https://example.org");
static const QString WEBDAV_PATH = QLatin1String("/dav/calendar");
void tst_CalDavClient::initTestCase()
{
    // Create a fake account
    mManager = new Accounts::Manager;
    QVERIFY(mManager->provider(QLatin1String("onlinesync")).isValid());
    mAccount = mManager->createAccount(QLatin1String("onlinesync"));
    QVERIFY(mAccount);
    mAccount->setEnabled(true);
    QVERIFY(mAccount->supportsService(QLatin1String("caldav")));
    Accounts::Service srv = mAccount->services(QLatin1String("caldav")).first();
    mAccount->selectService(srv);
    mAccount->setValue("caldav-sync/profile_id", mProfile.name());
    mAccount->setValue("server_address", SERVER_ADDRESS);
    mAccount->setValue("ignore_ssl_errors", true);
    mAccount->setCredentialsId(1);
    mAccount->setEnabled(true);
    QVERIFY(mAccount->syncAndBlock());
    QVERIFY(mAccount->id() > 0);
    mProfile.setKey(Buteo::KEY_ACCOUNT_ID, QString::number(mAccount->id()));
}

void tst_CalDavClient::cleanupTestCase()
{
    mAccount->remove();
    QVERIFY(mAccount->syncAndBlock());
    delete mAccount;
    delete mManager;
}

void tst_CalDavClient::init()
{
}

void tst_CalDavClient::cleanup()
{
}

void tst_CalDavClient::initConfig()
{
    // profile argument is copied at construction time.
    CalDavClient client(QLatin1String("caldav"), mProfile, nullptr);

    QVERIFY(client.init());
    QVERIFY(client.mService);
    QCOMPARE(client.mService->account()->id(), mAccount->id());
    QCOMPARE(client.mDAV->serverAddress(), SERVER_ADDRESS);
    QVERIFY(client.mDAV->ignoreSSLErrors());
}

void tst_CalDavClient::initConfigWithSettingsInAccount()
{
    Accounts::Account* account;
    Buteo::SyncProfile profile(QLatin1String("test_profile_at_root"));

    // Create a fake account with settings defined in the account.
    QVERIFY(mManager->provider(QLatin1String("onlinesync")).isValid());
    account = mManager->createAccount(QLatin1String("onlinesync"));
    QVERIFY(account);
    account->setEnabled(true);
    account->setValue("server_address", SERVER_ADDRESS);
    account->setValue("webdav_path", WEBDAV_PATH);
    account->setValue("ignore_ssl_errors", true);
    QVERIFY(account->supportsService(QLatin1String("caldav")));
    Accounts::Service srv = account->services(QLatin1String("caldav")).first();
    account->selectService(srv);
    account->setValue("caldav-sync/profile_id", profile.name());
    account->setCredentialsId(1);
    account->setEnabled(true);
    QVERIFY(account->syncAndBlock());
    QVERIFY(account->id() > 0);
    profile.setKey(Buteo::KEY_ACCOUNT_ID, QString::number(account->id()));

    CalDavClient client(QLatin1String("caldav"), profile, nullptr);

    QVERIFY(client.init());
    QVERIFY(client.mService);
    QCOMPARE(client.mService->account()->id(), account->id());
    QCOMPARE(client.mDAV->serverAddress(), SERVER_ADDRESS);
    QVERIFY(client.mDAV->ignoreSSLErrors());

    account->remove();
    QVERIFY(account->syncAndBlock());
}

void tst_CalDavClient::addInitCalendars()
{
    mAccount->setValue("calendars", QStringList() << QLatin1String("/foo/") << QLatin1String("/bar%40plop/"));
    mAccount->setValue("enabled_calendars", QStringList() << QLatin1String("/bar%40plop/"));
    mAccount->setValue("calendar_display_names", QStringList() << QLatin1String("Foo") << QLatin1String("Bar"));
    mAccount->setValue("calendar_colors", QStringList() << QLatin1String("#FF0000") << QLatin1String("#00FF00"));
    QVERIFY(mAccount->syncAndBlock());
}

void tst_CalDavClient::loadAccountCalendars()
{
    CalDavClient client(QLatin1String("caldav"), mProfile, nullptr);
    QVERIFY(client.init());

    const QList<Buteo::Dav::CalendarInfo> &calendars = client.loadAccountCalendars();

    QCOMPARE(calendars.count(), 1);
    QCOMPARE(calendars.first().remotePath, QLatin1String("/bar%40plop/"));
    QCOMPARE(calendars.first().displayName, QLatin1String("Bar"));
    QCOMPARE(calendars.first().color, QLatin1String("#00FF00"));
    QVERIFY(calendars.first().userPrincipal.isEmpty());
}

void tst_CalDavClient::mergeAccountCalendars()
{
    CalDavClient client(QLatin1String("caldav"), mProfile, nullptr);
    client.mManager = mManager; // So we can share the same Account pointers.
    QVERIFY(client.init());

    QList<Buteo::Dav::CalendarInfo> remoteCalendars;
    remoteCalendars << Buteo::Dav::CalendarInfo{QLatin1String("/bar%40plop/"),
            QLatin1String("Bar"), QLatin1String("#0000FF"), QLatin1String("/principals/2")};
    remoteCalendars << Buteo::Dav::CalendarInfo{QLatin1String("/foo/"),
            QLatin1String("New foo"), QLatin1String("#FF0000"), QString()};
    remoteCalendars << Buteo::Dav::CalendarInfo{QLatin1String("/toto%40tutu/"),
            QLatin1String("Toto"), QLatin1String("#FF00FF"), QString()};

    const QList<Buteo::Dav::CalendarInfo> &calendars = client.mergeAccountCalendars(remoteCalendars);

    QCOMPARE(calendars.count(), 2);
    QCOMPARE(calendars[0].remotePath, QLatin1String("/bar%40plop/"));
    QCOMPARE(calendars[0].displayName, QLatin1String("Bar"));
    QCOMPARE(calendars[0].color, QLatin1String("#0000FF"));
    QCOMPARE(calendars[0].userPrincipal, QLatin1String("/principals/2"));
    QCOMPARE(calendars[1].remotePath, QLatin1String("/toto%40tutu/"));
    QCOMPARE(calendars[1].displayName, QLatin1String("Toto"));
    QCOMPARE(calendars[1].color, QLatin1String("#FF00FF"));
    QVERIFY(calendars[1].userPrincipal.isEmpty());

    // Also check that account has updated the disabled calendar.
    Accounts::Service srv = mAccount->services(QLatin1String("caldav")).first();
    mAccount->selectService(srv);
    const QStringList &allCalendars = mAccount->value("calendars").toStringList();
    QCOMPARE(allCalendars.count(), 3);
    QVERIFY(allCalendars.contains(QLatin1String("/foo/")));
    const QStringList &names = mAccount->value("calendar_display_names").toStringList();
    int at = allCalendars.indexOf(QLatin1String("/foo/"));
    QVERIFY(at < names.length());
    QCOMPARE(names[at], QLatin1String("New foo"));
}

void tst_CalDavClient::removeAccountCalendar()
{
    CalDavClient client(QLatin1String("caldav"), mProfile, nullptr);
    client.mManager = mManager; // So we can share the same Account pointers.
    QVERIFY(client.init());

    client.removeAccountCalendars(QStringList() << QLatin1String("/bar%40plop/") << QLatin1String("/notStoredOne/"));

    Accounts::Service srv = mAccount->services(QLatin1String("caldav")).first();
    mAccount->selectService(srv);
    const QStringList &allCalendars = mAccount->value("calendars").toStringList();
    QCOMPARE(allCalendars.count(), 2);
    QVERIFY(!allCalendars.contains(QLatin1String("/bar%40plop/")));
    const QStringList &names = mAccount->value("calendar_display_names").toStringList();
    QCOMPARE(names.count(), 2);
    QVERIFY(!names.contains(QLatin1String("Bar")));
}

#include "tst_caldavclient.moc"
QTEST_MAIN(tst_CalDavClient)
