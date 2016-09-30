/* -*- c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2016 Caliste Damien.
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

#include <notebooksyncagent.h>
#include <extendedcalendar.h>
#include <settings.h>
#include <QNetworkAccessManager>
#include <QFile>
#include <QtGlobal>

class tst_NotebookSyncAgent : public QObject
{
    Q_OBJECT

public:
    tst_NotebookSyncAgent();
    virtual ~tst_NotebookSyncAgent();

public slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

private slots:
    void insertEvent_data();
    void insertEvent();

private:
    Settings m_settings;
    NotebookSyncAgent *m_agent;
};

tst_NotebookSyncAgent::tst_NotebookSyncAgent()
    : m_agent(0)
{
}

tst_NotebookSyncAgent::~tst_NotebookSyncAgent()
{
}

void tst_NotebookSyncAgent::initTestCase()
{
    qputenv("SQLITESTORAGEDB", "./db");
    qputenv("MSYNCD_LOGGING_LEVEL", "8");

    QFile::remove("./db");
}

void tst_NotebookSyncAgent::cleanupTestCase()
{    
}

void tst_NotebookSyncAgent::init()
{
    mKCal::ExtendedCalendar::Ptr cal = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar( QLatin1String( "UTC" ) ));
    mKCal::ExtendedStorage::Ptr store = mKCal::ExtendedCalendar::defaultStorage(cal);

    store->open();

    QNetworkAccessManager *mNAManager = new QNetworkAccessManager();
    m_agent = new NotebookSyncAgent(cal, store, mNAManager, &m_settings, QLatin1String("/testCal/"));
    mKCal::Notebook *notebook = new mKCal::Notebook("123456789", "test1", "test 1", "red", true, false, false, false, false);

    m_agent->mNotebook = mKCal::Notebook::Ptr(notebook);

}

void tst_NotebookSyncAgent::cleanup()
{
    m_agent->mStorage->save();
    m_agent->mStorage->close();

    delete m_agent->mNetworkManager;
    delete m_agent;
}

void tst_NotebookSyncAgent::insertEvent_data()
{
    QTest::addColumn<QString>("xmlFilename");
    QTest::addColumn<QString>("expectedUID");
    QTest::addColumn<QString>("expectedSummary");
    QTest::addColumn<QString>("expectedRecurrenceID");
    QTest::addColumn<int>("expectedNAlarms");
    
    QTest::newRow("simple event response")
        << "data/notebooksyncagent_simple.xml"
        << QStringLiteral("NBUID:123456789:972a7c13-bbd6-4fce-9ebb-03a808111828")
        << QStringLiteral("Test")
        << QString()
        << 1;
    QTest::newRow("recurring event response")
        << "data/notebooksyncagent_recurring.xml"
        << QStringLiteral("NBUID:123456789:7d145c8e-0f34-45a0-b8ca-d9c86093bc11")
        << QStringLiteral("My Event")
        << QStringLiteral("2012-11-09T10:00:00Z")
        << 0;
    QTest::newRow("insert and update event response")
        << "data/notebooksyncagent_insert_and_update.xml"
        << QStringLiteral("NBUID:123456789:7d145c8e-0f34-45a0-b8ca-d9c86093bc12")
        << QStringLiteral("My Event 2")
        << QString()
        << 0;
}

void tst_NotebookSyncAgent::insertEvent()
{
    QFETCH(QString, xmlFilename);
    QFETCH(QString, expectedUID);
    QFETCH(QString, expectedSummary);
    QFETCH(QString, expectedRecurrenceID);
    QFETCH(int, expectedNAlarms);

    QFile f(QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), xmlFilename));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        QFAIL("Data file does not exist or cannot be opened for reading!");
    }

    Reader rd;
    rd.read(f.readAll());

    QVERIFY(m_agent->updateIncidences(rd.results().values()));

    KCalCore::Incidence::List incidences = m_agent->mCalendar->incidences();

    KCalCore::Incidence::Ptr ev;
    if (expectedRecurrenceID.isEmpty())
        ev = m_agent->mCalendar->event(expectedUID);
    else
        ev = m_agent->mCalendar->event(expectedUID, KDateTime::fromString(expectedRecurrenceID));
    QVERIFY(ev);
    
    QCOMPARE(ev->uid(), expectedUID);
    QCOMPARE(ev->summary(), expectedSummary);

    QCOMPARE(ev->alarms().length(), expectedNAlarms);
}

#include "tst_notebooksyncagent.moc"
QTEST_MAIN(tst_NotebookSyncAgent)
