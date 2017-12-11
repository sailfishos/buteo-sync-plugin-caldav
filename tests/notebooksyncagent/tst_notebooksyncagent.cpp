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

#include <incidence.h>
#include <event.h>
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

    void insertMultipleEvents_data();
    void insertMultipleEvents();

    void removePossibleLocal_data();
    void removePossibleLocal();

    void updateEvent();

private:
    Settings m_settings;
    NotebookSyncAgent *m_agent;

    typedef QMap<QString, QString> IncidenceDescr;
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
    mKCal::Notebook::List notebookList = m_agent->mStorage->notebooks();
    Q_FOREACH (mKCal::Notebook::Ptr notebook, notebookList) {
        m_agent->mStorage->deleteNotebook(notebook);
    }

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

    QVERIFY(m_agent->updateIncidences(rd.results()));

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

void tst_NotebookSyncAgent::insertMultipleEvents_data()
{
    QTest::addColumn<QString>("xmlFilename");
    QTest::addColumn<QStringList>("expectedUIDs");
    QTest::addColumn<QStringList>("expectedSummaries");
    QTest::addColumn<QStringList>("expectedRecurrenceIDs");

    QTest::newRow("singleA, orphan1, singleB, orphan2")
        << "data/notebooksyncagent_orphanexceptions.xml"
        << (QStringList() << QStringLiteral("NBUID:123456789:aaaaaaaaaa")
                          << QStringLiteral("NBUID:123456789:d1158dac-5a63-49d5-83b8-176bb792a088")
                          << QStringLiteral("NBUID:123456789:bbbbbbbbbb")
                          << QStringLiteral("NBUID:123456789:d1158dac-5a63-49d5-83b8-176bb792a088"))
        << (QStringList() << QStringLiteral("Test a")
                          << QStringLiteral("Test orphan one")
                          << QStringLiteral("Test b")
                          << QStringLiteral("Test orphan two"))
        << (QStringList() << QString()
                          << QStringLiteral("20170323T110000Z")
                          << QString()
                          << QStringLiteral("20170330T110000Z"));
}

void tst_NotebookSyncAgent::insertMultipleEvents()
{
    QFETCH(QString, xmlFilename);
    QFETCH(QStringList, expectedUIDs);
    QFETCH(QStringList, expectedSummaries);
    QFETCH(QStringList, expectedRecurrenceIDs);

    QVERIFY(expectedUIDs.size() == expectedSummaries.size());
    QVERIFY(expectedSummaries.size() == expectedRecurrenceIDs.size());

    QFile f(QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), xmlFilename));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        QFAIL("Data file does not exist or cannot be opened for reading!");
    }

    Reader rd;
    rd.read(f.readAll());

    QVERIFY(m_agent->updateIncidences(rd.results()));
    KCalCore::Incidence::List incidences = m_agent->mCalendar->incidences();
    for (int i = 0; i < expectedUIDs.size(); ++i) {
        KCalCore::Incidence::Ptr ev;
        if (expectedRecurrenceIDs[i].isEmpty()) {
            ev = m_agent->mCalendar->event(expectedUIDs[i]);
        } else {
            KDateTime recId = KDateTime::fromString(expectedRecurrenceIDs[i]);
            ev = m_agent->mCalendar->event(expectedUIDs[i], recId);
        }
        qWarning() << "Trying to find event:" << expectedUIDs[i] << expectedRecurrenceIDs[i];
        QVERIFY(ev);
        QCOMPARE(ev->uid(), expectedUIDs[i]);
        QCOMPARE(ev->summary(), expectedSummaries[i]);
    }
}

void tst_NotebookSyncAgent::removePossibleLocal_data()
{
    QTest::addColumn<IncidenceDescr>("remoteIncidence");
    QTest::addColumn<QList<IncidenceDescr> >("localIncidences");
    QTest::addColumn<bool>("expectedRemoval");
    
    IncidenceDescr remote;
    remote.insert("uri", QStringLiteral("/bob/calendar/12346789.ics"));
    remote.insert("recurrenceId", QStringLiteral("Wed Mar 15 14:46:08 UTC 2017"));

    {
        QList<IncidenceDescr> locals;
        IncidenceDescr event;
        event.insert("uri", QStringLiteral("/bob/calendar/12346789.ics"));
        event.insert("recurrenceId", QStringLiteral("Wed Mar 15 14:46:08 UTC 2017"));
        locals << event;
        event.clear();
        event.insert("uri", QStringLiteral("/alice/calendar/147852369.ics"));
        event.insert("recurrenceId", QStringLiteral("Thu Mar 16 05:12:45 UTC 2017"));
        locals << event;
        QTest::newRow("not a local modification")
            << remote
            << locals
            << true;
    }

    {
        QList<IncidenceDescr> locals;
        IncidenceDescr event;
        event.insert("uri", QStringLiteral("/bob/calendar/12346789.ics"));
        event.insert("recurrenceId", QStringLiteral("Wed Mar 15 14:46:08 UTC 2017"));
        event.insert("description", QStringLiteral("modified description"));
        locals << event;
        event.clear();
        event.insert("uri", QStringLiteral("/alice/calendar/147852369.ics"));
        event.insert("recurrenceId", QStringLiteral("Thu Mar 16 05:12:45 UTC 2017"));
        locals << event;
        QTest::newRow("a valid local modification")
            << remote
            << locals
            << false;
    }
}

static QString fetchUri(KCalCore::Incidence::Ptr incidence)
{
    Q_FOREACH (const QString &comment, incidence->comments()) {
        if (comment.startsWith("buteo:caldav:uri:")) {
            QString uri = comment.mid(17);
            return uri;
        }
    }
    return QString();
}

void tst_NotebookSyncAgent::removePossibleLocal()
{
    QFETCH(IncidenceDescr, remoteIncidence);
    QFETCH(QList<IncidenceDescr>, localIncidences);
    QFETCH(bool, expectedRemoval);

    KCalCore::Incidence::List localModifications;
    Q_FOREACH (const IncidenceDescr &descr, localIncidences) {
        KCalCore::Incidence::Ptr local = KCalCore::Incidence::Ptr(new KCalCore::Event);
        local->setRecurrenceId(KDateTime::fromString(descr["recurrenceId"]));
        local->addComment(QStringLiteral("buteo:caldav:uri:%1").arg(descr["uri"]));
        if (descr.contains("description"))
            local->setDescription(descr["description"]);
        localModifications << local;
    }

    Reader::CalendarResource resource;
    resource.href = remoteIncidence["uri"];
    KCalCore::Incidence::Ptr remote = KCalCore::Incidence::Ptr(new KCalCore::Event);
    remote->setRecurrenceId(KDateTime::fromString(remoteIncidence["recurrenceId"]));
    remote->addComment(QStringLiteral("buteo:caldav:uri:%1").arg(remoteIncidence["uri"]));
    resource.incidences << remote;

    QList<KDateTime> recurrenceIds;
    recurrenceIds << KDateTime::fromString(remoteIncidence["recurrenceId"]);
    QMultiHash<QString, KDateTime> addedPersistentExceptionOccurrences;

    m_agent->removePossibleLocalModificationIfIdentical
        (resource.href, recurrenceIds, resource,
         addedPersistentExceptionOccurrences, &localModifications);

    /* Check if remote is still or not in localModifications. */
    bool found = false;
    Q_FOREACH (const KCalCore::Incidence::Ptr &incidence, localModifications) {
        if (fetchUri(incidence) == fetchUri(remote)) {
            found = true;
            if (expectedRemoval)
                QFAIL("Expected removal but still present.");
        }
    }
    if (!expectedRemoval && !found)
        QFAIL("Local modification not found anymore.");
}

void tst_NotebookSyncAgent::updateEvent()
{
    // Populate the database.
    KCalCore::Incidence::Ptr incidence = KCalCore::Incidence::Ptr(new KCalCore::Event);
    incidence->setUid("123456-moz");
    incidence->setNonKDECustomProperty("X-MOZ-LASTACK", "20171013T174424Z");
    m_agent->mCalendar->addEvent(incidence.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    m_agent->mStorage->save();

    // Test that event exists.
    incidence = m_agent->mCalendar->event(QStringLiteral("123456-moz"));
    QVERIFY(incidence);
    QCOMPARE(incidence->customProperties().count(), 1);
    QCOMPARE(incidence->nonKDECustomProperty("X-MOZ-LASTACK"),
             QStringLiteral("20171013T174424Z"));

    // Update event with a custom property.
    KCalCore::Incidence::Ptr update = KCalCore::Incidence::Ptr(new KCalCore::Event);
    update->setUid("123456-moz");
    update->setNonKDECustomProperty("X-MOZ-LASTACK", "20171016T174424Z");
    bool success = true;
    m_agent->updateIncidence(update, QString(), QString(), false, &success);
    QVERIFY(success);

    // Check that custom property is updated as well.
    incidence = m_agent->mCalendar->event(QStringLiteral("123456-moz"));
    QVERIFY(incidence);
    QCOMPARE(incidence->customProperties().count(), 2);
    QCOMPARE(incidence->nonKDECustomProperty("X-MOZ-LASTACK"),
             QStringLiteral("20171016T174424Z"));
}

#include "tst_notebooksyncagent.moc"
QTEST_MAIN(tst_NotebookSyncAgent)
