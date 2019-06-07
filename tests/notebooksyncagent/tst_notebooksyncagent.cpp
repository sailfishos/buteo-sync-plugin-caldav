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

#include "incidencehandler.h"

#include <memorycalendar.h>
#include <icalformat.h>
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
    void updateHrefETag();
    void calculateDelta();

    void oneDownSyncCycle_data();
    void oneDownSyncCycle();

    void oneUpSyncCycle_data();
    void oneUpSyncCycle();

    void updateIncidence_data();
    void updateIncidence();

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
    mKCal::Notebook::Ptr notebook = m_agent->mStorage->notebook("123456789");
    m_agent->mStorage->deleteNotebook(notebook);

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
    remote.insert("recurrenceId", QStringLiteral("2017-03-15T14:46:08Z"));

    {
        QList<IncidenceDescr> locals;
        IncidenceDescr event;
        event.insert("uri", QStringLiteral("/bob/calendar/12346789.ics"));
        event.insert("recurrenceId", QStringLiteral("2017-03-15T14:46:08Z"));
        locals << event;
        event.clear();
        event.insert("uri", QStringLiteral("/alice/calendar/147852369.ics"));
        event.insert("recurrenceId", QStringLiteral("2017-03-16T05:12:45Z"));
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
        event.insert("recurrenceId", QStringLiteral("2017-03-15T14:46:08Z"));
        event.insert("description", QStringLiteral("modified description"));
        locals << event;
        event.clear();
        event.insert("uri", QStringLiteral("/alice/calendar/147852369.ics"));
        event.insert("recurrenceId", QStringLiteral("2017-03-16T05:12:45Z"));
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
static QString fetchETag(KCalCore::Incidence::Ptr incidence)
{
    const QStringList &comments(incidence->comments());
    Q_FOREACH (const QString &comment, comments) {
        if (comment.startsWith("buteo:caldav:etag:")) {
            return comment.mid(18);
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
    resource.incidences << remote;

    m_agent->removePossibleLocalModificationIfIdentical(resource, &localModifications);

    /* Check if remote is still or not in localModifications. */
    bool found = false;
    Q_FOREACH (const KCalCore::Incidence::Ptr &incidence, localModifications) {
        if (fetchUri(incidence) == remoteIncidence["uri"]) {
            found = true;
        }
    }
    if (expectedRemoval && found)
        QFAIL("Expected removal but still present.");
    if (!expectedRemoval && !found)
        QFAIL("Local modification not found anymore.");
}

void tst_NotebookSyncAgent::updateEvent()
{
    // Populate the database.
    KCalCore::Incidence::Ptr incidence = KCalCore::Incidence::Ptr(new KCalCore::Event);
    incidence->setUid("123456-moz");
    incidence->setNonKDECustomProperty("X-MOZ-LASTACK", "20171013T174424Z");
    incidence->setCreated(KDateTime(QDate(2019, 03, 28)));
    incidence->setAllDay(false);

    QVERIFY(m_agent->mCalendar->addEvent(incidence.staticCast<KCalCore::Event>(),
                                         m_agent->mNotebook->uid()));
    m_agent->mStorage->save();

    // Test that event exists.
    incidence = m_agent->mCalendar->event(QStringLiteral("123456-moz"));
    QVERIFY(incidence);
    QCOMPARE(incidence->customProperties().count(), 1);
    QCOMPARE(incidence->nonKDECustomProperty("X-MOZ-LASTACK"),
             QStringLiteral("20171013T174424Z"));
    QCOMPARE(incidence->created().date(), QDate(2019, 03, 28));

    // Update event with a custom property.
    KCalCore::Incidence::Ptr update = KCalCore::Incidence::Ptr(new KCalCore::Event);
    update->setUid("123456-moz");
    update->setNonKDECustomProperty("X-MOZ-LASTACK", "20171016T174424Z");
    update->setAllDay(false);
    bool success = true;
    m_agent->updateIncidence(update, "/testPath/123456.ics", "\"123456\"", false, &success);
    QVERIFY(success);

    // Check that custom property is updated as well.
    incidence = m_agent->mCalendar->event(QStringLiteral("123456-moz"));
    QVERIFY(incidence);
    QCOMPARE(incidence->customProperties().count(), 1);
    QCOMPARE(incidence->nonKDECustomProperty("X-MOZ-LASTACK"),
             QStringLiteral("20171016T174424Z"));
}

void tst_NotebookSyncAgent::updateHrefETag()
{
    // Populate the database.
    KCalCore::Incidence::Ptr incidence = KCalCore::Incidence::Ptr(new KCalCore::Event);
    incidence->setUid("123456-single");
    m_agent->mCalendar->addEvent(incidence.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    incidence = KCalCore::Incidence::Ptr(new KCalCore::Event);
    incidence->setUid("123456-recurs");
    incidence->setDtStart(KDateTime::currentUtcDateTime());
    incidence->recurrence()->setDaily(1);
    incidence->recurrence()->setDuration(28);
    m_agent->mCalendar->addEvent(incidence.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    KDateTime refId = incidence->recurrence()->getNextDateTime(incidence->dtStart().addDays(4));
    incidence = m_agent->mCalendar->dissociateSingleOccurrence(incidence, refId,
                                                               refId.timeSpec());
    m_agent->mCalendar->addEvent(incidence.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    m_agent->mStorage->save();

    // Simple case.
    m_agent->updateHrefETag(QStringLiteral("123456-single"),
                            QStringLiteral("/testCal/123456-single.ics"),
                            QStringLiteral("\"123456\""));

    incidence = m_agent->mCalendar->event(QStringLiteral("123456-single"));
    QCOMPARE(fetchUri(incidence), QStringLiteral("/testCal/123456-single.ics"));
    QCOMPARE(fetchETag(incidence), QStringLiteral("\"123456\""));

    // Update simple case.
    m_agent->updateHrefETag(QStringLiteral("123456-single"),
                            QStringLiteral("/testCal/123456-single.ics"),
                            QStringLiteral("\"456789\""));

    incidence = m_agent->mCalendar->event(QStringLiteral("123456-single"));
    QCOMPARE(fetchUri(incidence), QStringLiteral("/testCal/123456-single.ics"));
    QCOMPARE(fetchETag(incidence), QStringLiteral("\"456789\""));

    // Recuring case.
    m_agent->updateHrefETag(QStringLiteral("123456-recurs"),
                            QStringLiteral("/testCal/123456-recurs.ics"),
                            QStringLiteral("\"123456\""));

    incidence = m_agent->mCalendar->event(QStringLiteral("123456-recurs"));
    QCOMPARE(fetchUri(incidence), QStringLiteral("/testCal/123456-recurs.ics"));
    QCOMPARE(fetchETag(incidence), QStringLiteral("\"123456\""));
    incidence = m_agent->mCalendar->event(QStringLiteral("123456-recurs"), refId);
    QCOMPARE(fetchUri(incidence), QStringLiteral("/testCal/123456-recurs.ics"));
    QCOMPARE(fetchETag(incidence), QStringLiteral("\"123456\""));

    // Update recuring case.
    m_agent->updateHrefETag(QStringLiteral("123456-recurs"),
                            QStringLiteral("/testCal/123456-recurs.ics"),
                            QStringLiteral("\"456789\""));

    incidence = m_agent->mCalendar->event(QStringLiteral("123456-recurs"));
    QCOMPARE(fetchUri(incidence), QStringLiteral("/testCal/123456-recurs.ics"));
    QCOMPARE(fetchETag(incidence), QStringLiteral("\"456789\""));
    incidence = m_agent->mCalendar->event(QStringLiteral("123456-recurs"), refId);
    QCOMPARE(fetchUri(incidence), QStringLiteral("/testCal/123456-recurs.ics"));
    QCOMPARE(fetchETag(incidence), QStringLiteral("\"456789\""));
}

static bool incidenceListContains(const KCalCore::Incidence::List &list,
                                  const KCalCore::Incidence::Ptr &ev)
{
    for (KCalCore::Incidence::List::ConstIterator it = list.constBegin();
         it != list.constEnd(); it++) {
        if ((*it)->uid() == ev->uid()
            && (!ev->hasRecurrenceId()
                || (*it)->recurrenceId() == ev->recurrenceId())) {
            return true;
        }
    }
    return false;
}

void tst_NotebookSyncAgent::calculateDelta()
{
    QHash<QString, QString> remoteUriEtags;

    // Populate the database.
    KCalCore::Incidence::Ptr ev222 = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev222->setSummary("local modification");
    ev222->addComment(QStringLiteral("buteo:caldav:uri:%1222.ics").arg(m_agent->mRemoteCalendarPath));
    ev222->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag222"));
    m_agent->mCalendar->addEvent(ev222.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalCore::Incidence::Ptr ev333 = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev333->setSummary("local deletion");
    ev333->addComment(QStringLiteral("buteo:caldav:uri:%1333.ics").arg(m_agent->mRemoteCalendarPath));
    ev333->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag333"));
    m_agent->mCalendar->addEvent(ev333.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalCore::Incidence::Ptr ev444 = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev444->addComment(QStringLiteral("buteo:caldav:uri:%1444.ics").arg(m_agent->mRemoteCalendarPath));
    ev444->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag444"));
    ev444->setSummary("local modification discarded by a remote modification");
    m_agent->mCalendar->addEvent(ev444.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalCore::Incidence::Ptr ev555 = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev555->addComment(QStringLiteral("buteo:caldav:uri:%1555.ics").arg(m_agent->mRemoteCalendarPath));
    ev555->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag555"));
    ev555->setSummary("local modification discarded by a remote deletion");
    m_agent->mCalendar->addEvent(ev555.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalCore::Incidence::Ptr ev666 = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev666->addComment(QStringLiteral("buteo:caldav:uri:%1666.ics").arg(m_agent->mRemoteCalendarPath));
    ev666->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag666"));
    ev666->setSummary("remote modification");
    m_agent->mCalendar->addEvent(ev666.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalCore::Incidence::Ptr ev777 = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev777->addComment(QStringLiteral("buteo:caldav:uri:%1777.ics").arg(m_agent->mRemoteCalendarPath));
    ev777->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag777"));
    ev777->setSummary("remote deletion");
    m_agent->mCalendar->addEvent(ev777.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalCore::Incidence::Ptr ev888 = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev888->setRecurrenceId(KDateTime());
    ev888->addComment(QStringLiteral("buteo:caldav:uri:%1888.ics").arg(m_agent->mRemoteCalendarPath));
    ev888->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag888"));
    ev888->setSummary("unchanged synced incidence");
    KDateTime recId = KDateTime::currentUtcDateTime();
    ev888->setDtStart( recId );
    ev888->recurrence()->addRDateTime(recId);
    m_agent->mCalendar->addEvent(ev888.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalCore::Incidence::Ptr ev112 = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev112->setSummary("partial local addition, need download");
    m_agent->mCalendar->addEvent(ev112.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    m_agent->mStorage->save();
    KCalCore::Incidence::Ptr ev113 = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev113->setSummary("partial local modification, need upload");
    m_agent->mCalendar->addEvent(ev113.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    m_agent->mStorage->save();

    KDateTime lastSync = KDateTime::currentUtcDateTime();
    m_agent->mNotebook->setSyncDate(lastSync.addSecs(1));

    // Sleep a bit to ensure that modification done after the sleep will have
    // dates that are later than creation ones, so inquiring the local database
    // with modifications strictly later than lastSync value will actually
    // returned the right events and not all.
    QThread::sleep(3);

    // Perform local modifications.
    KCalCore::Incidence::Ptr ev111 = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev111->setSummary("local addition");
    ev113->setDescription(QStringLiteral("Modified summary."));
    m_agent->mCalendar->addEvent(ev111.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    ev222->setDescription(QStringLiteral("Modified summary."));
    m_agent->mCalendar->deleteIncidence(ev333);
    ev444->setDescription(QStringLiteral("Modified summary."));
    ev555->setDescription(QStringLiteral("Modified summary."));
    KCalCore::Incidence::Ptr ev999 = m_agent->mCalendar->dissociateSingleOccurrence(ev888, recId, recId.timeSpec());
    QVERIFY(ev999);
    ev999->setSummary("local addition of persistent exception");
    m_agent->mCalendar->addEvent(ev999.staticCast<KCalCore::Event>(),
                                 m_agent->mNotebook->uid());
    m_agent->mStorage->save();

    // Generate server etag reply.
    remoteUriEtags.insert(QStringLiteral("%1000.ics").arg(m_agent->mRemoteCalendarPath),
                          QStringLiteral("\"etag000\""));
    remoteUriEtags.insert(QStringLiteral("%1%2.ics").arg(m_agent->mRemoteCalendarPath).arg(ev112->uid()),
                          QStringLiteral("\"etag112\""));
    remoteUriEtags.insert(QStringLiteral("%1%2.ics").arg(m_agent->mRemoteCalendarPath).arg(ev113->uid()),
                          QStringLiteral("\"etag113\""));
    remoteUriEtags.insert(QStringLiteral("%1222.ics").arg(m_agent->mRemoteCalendarPath),
                          QStringLiteral("\"etag222\""));
    remoteUriEtags.insert(QStringLiteral("%1333.ics").arg(m_agent->mRemoteCalendarPath),
                          QStringLiteral("\"etag333\""));
    remoteUriEtags.insert(QStringLiteral("%1444.ics").arg(m_agent->mRemoteCalendarPath),
                          QStringLiteral("\"etag444-1\""));
    remoteUriEtags.insert(QStringLiteral("%1666.ics").arg(m_agent->mRemoteCalendarPath),
                          QStringLiteral("\"etag666-1\""));
    remoteUriEtags.insert(QStringLiteral("%1888.ics").arg(m_agent->mRemoteCalendarPath),
                          QStringLiteral("\"etag888\""));

    QVERIFY(m_agent->calculateDelta(remoteUriEtags,
                                    &m_agent->mLocalAdditions,
                                    &m_agent->mLocalModifications,
                                    &m_agent->mLocalDeletions,
                                    &m_agent->mRemoteAdditions,
                                    &m_agent->mRemoteModifications,
                                    &m_agent->mRemoteDeletions));
    QCOMPARE(m_agent->mLocalAdditions.count(), 2);
    QVERIFY(incidenceListContains(m_agent->mLocalAdditions, ev111));
    QVERIFY(incidenceListContains(m_agent->mLocalAdditions, ev999));
    QCOMPARE(m_agent->mLocalModifications.count(), 3);
    QVERIFY(incidenceListContains(m_agent->mLocalModifications, ev222));
    QVERIFY(incidenceListContains(m_agent->mLocalModifications, ev113));
    QVERIFY(incidenceListContains(m_agent->mLocalModifications, ev888));
    // ev444 have been locally modified, but is not in mLocalModifications
    // because of precedence of remote modifications by default.
    QCOMPARE(m_agent->mLocalDeletions.count(), 1);
    QCOMPARE(m_agent->mLocalDeletions.first()->uid(), ev333->uid());
    QCOMPARE(m_agent->mRemoteAdditions.count(), 1);
    QCOMPARE(m_agent->mRemoteAdditions.first(), QStringLiteral("%1000.ics").arg(m_agent->mRemoteCalendarPath));
    QCOMPARE(m_agent->mRemoteModifications.count(), 3);
    QVERIFY(m_agent->mRemoteModifications.contains
            (QStringLiteral("%1%2.ics").arg(m_agent->mRemoteCalendarPath).arg(ev112->uid())));
    QVERIFY(m_agent->mRemoteModifications.contains
            (QStringLiteral("%1444.ics").arg(m_agent->mRemoteCalendarPath)));
    QVERIFY(m_agent->mRemoteModifications.contains
            (QStringLiteral("%1666.ics").arg(m_agent->mRemoteCalendarPath)));
    uint nFound = 0;
    Q_FOREACH(const KCalCore::Incidence::Ptr &incidence, m_agent->mRemoteDeletions) {
        if (incidence->uid() == ev555->uid()
            || incidence->uid() == ev777->uid()) {
            nFound += 1;
        }
    }
    QCOMPARE(nFound, uint(2));
}

Q_DECLARE_METATYPE(KCalCore::Incidence::Ptr)
void tst_NotebookSyncAgent::oneDownSyncCycle_data()
{
    QTest::addColumn<QString>("notebookId");
    QTest::addColumn<QString>("uid");
    QTest::addColumn<KCalCore::Incidence::List>("events");
    KCalCore::Incidence::Ptr ev;

    ev = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev->setSummary("Simple event");
    QTest::newRow("simple event")
        << QStringLiteral("notebook-down-1")
        << QStringLiteral("111") << (KCalCore::Incidence::List() << ev);

    ev = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev->setSummary("Recurent event");
    ev->setDtStart(KDateTime::currentUtcDateTime());
    ev->recurrence()->setDaily(1);
    ev->recurrence()->setDuration(28);
    KDateTime refId = ev->recurrence()->getNextDateTime(ev->dtStart().addDays(4));
    KCalCore::Incidence::Ptr ex =
        m_agent->mCalendar->dissociateSingleOccurrence(ev, refId, refId.timeSpec());
    ex->setSummary("Persistent exception");
    QTest::newRow("recurent event with exception")
        << QStringLiteral("notebook-down-2")
        << QStringLiteral("222") << (KCalCore::Incidence::List() << ev << ex);

    refId = ev->recurrence()->getNextDateTime(ev->dtStart().addDays(2));
    ex = m_agent->mCalendar->dissociateSingleOccurrence(ev, refId, refId.timeSpec());
    ex->setSummary("orphan event");
    QTest::newRow("orphan persistent exception event")
        << QStringLiteral("notebook-down-3")
        << QStringLiteral("333") << (KCalCore::Incidence::List() << ex);

    ex->setSummary("modified persistent exception event");
    QTest::newRow("modified persistent exception event")
        << QStringLiteral("notebook-down-3")
        << QStringLiteral("333") << (KCalCore::Incidence::List() << ex);
}

void tst_NotebookSyncAgent::oneDownSyncCycle()
{
    QFETCH(QString, notebookId);
    QFETCH(QString, uid);
    QFETCH(KCalCore::Incidence::List, events);
    QHash<QString, QString> remoteUriEtags;

    /* We read or create a notebook for this test. */
    mKCal::Notebook::Ptr notebook = m_agent->mStorage->notebook(notebookId);
    if (notebook) {
        QVERIFY(m_agent->mStorage->loadNotebookIncidences(notebook->uid()));
    } else {
        notebook = mKCal::Notebook::Ptr(new mKCal::Notebook(notebookId, "test1", "test 1", "red", true, false, false, false, false));
        m_agent->mStorage->addNotebook(notebook);
    }
    m_agent->mNotebook = notebook;
    KCalCore::MemoryCalendar::Ptr memoryCalendar(new KCalCore::MemoryCalendar(KDateTime::UTC));
    for (KCalCore::Incidence::List::Iterator it = events.begin();
         it != events.end(); it++) {
        (*it)->setUid(uid);
        if ((*it)->recurs()) {
            Q_FOREACH (KCalCore::Incidence::Ptr instance, events) {
                KCalCore::DateTimeList exDateTimes =
                    (*it)->recurrence()->exDateTimes();
                exDateTimes.removeAll(instance->recurrenceId());
                (*it)->recurrence()->setExDateTimes(exDateTimes);
            }
        }
        memoryCalendar->addIncidence(*it);
    }

    KCalCore::ICalFormat icalFormat;
    QString uri(QStringLiteral("/testCal/%1.ics").arg(uid));
    QString etag(QStringLiteral("\"etag-%1\"").arg(uid));
    QString response("<?xml version=\"1.0\"?>\n"
                     "<d:multistatus xmlns:d=\"DAV:\" xmlns:cal=\"urn:ietf:params:xml:ns:caldav\">\n");
    response += QString(" <d:response>\n"
                        "  <d:href>%1</d:href>\n"
                        "  <d:propstat>\n"
                        "   <d:prop>\n"
                        "    <d:getetag>%2</d:getetag>\n").arg(uri).arg(etag);
    response += QString("    <cal:calendar-data>\n");
    response += icalFormat.toString(memoryCalendar, QString(), false);
    response += QString("    </cal:calendar-data>\n");
    response += QString("   </d:prop>\n"
                        "   <d:status>HTTP/1.1 200 OK</d:status>\n"
                        "  </d:propstat>\n"
                        " </d:response>\n"
                        "</d:multistatus>\n");
    remoteUriEtags.insert(uri, etag);

    // Populate the database with the initial import, like in a slow sync.
    Reader reader;
    reader.read(response.toUtf8());
    QVERIFY(!reader.hasError());
    QCOMPARE(reader.results().count(), 1);
    QCOMPARE(reader.results()[0].incidences.count(), events.count());

    QVERIFY(m_agent->updateIncidences(QList<Reader::CalendarResource>() << reader.results()));
    m_agent->mStorage->save();
    m_agent->mNotebook->setSyncDate(KDateTime::currentUtcDateTime());

    // Compute delta and check that nothing has changed indeed.
    QVERIFY(m_agent->calculateDelta(remoteUriEtags,
                                    &m_agent->mLocalAdditions,
                                    &m_agent->mLocalModifications,
                                    &m_agent->mLocalDeletions,
                                    &m_agent->mRemoteAdditions,
                                    &m_agent->mRemoteModifications,
                                    &m_agent->mRemoteDeletions));
    QCOMPARE(m_agent->mLocalAdditions.count(), 0);
    QCOMPARE(m_agent->mLocalModifications.count(), 0);
    QCOMPARE(m_agent->mLocalDeletions.count(), 0);
    QCOMPARE(m_agent->mRemoteAdditions.count(), 0);
    QCOMPARE(m_agent->mRemoteModifications.count(), 0);
    QCOMPARE(m_agent->mRemoteDeletions.count(), 0);
}

void tst_NotebookSyncAgent::oneUpSyncCycle_data()
{
    QTest::addColumn<QString>("uid");
    QTest::addColumn<KCalCore::Incidence::List>("events");
    KCalCore::Incidence::Ptr ev;

    ev = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev->setSummary("Simple added event");
    QTest::newRow("simple added event")
        << QStringLiteral("100")
        << (KCalCore::Incidence::List() << ev);

    ev = KCalCore::Incidence::Ptr(new KCalCore::Event);
    ev->setSummary("Recurent event");
    ev->setDtStart(KDateTime::currentUtcDateTime());
    ev->recurrence()->setDaily(1);
    ev->recurrence()->setDuration(28);
    KDateTime refId = ev->recurrence()->getNextDateTime(ev->dtStart().addDays(4));
    KCalCore::Incidence::Ptr ex =
        m_agent->mCalendar->dissociateSingleOccurrence(ev, refId, refId.timeSpec());
    ex->setSummary("Persistent exception");
    QTest::newRow("added recurent event with exception")
        << QStringLiteral("200")
        << (KCalCore::Incidence::List() << ev << ex);
}

void tst_NotebookSyncAgent::oneUpSyncCycle()
{
    QFETCH(QString, uid);
    QFETCH(KCalCore::Incidence::List, events);
    QHash<QString, QString> remoteUriEtags;
    static int id = 0;

    /* We create a notebook for this test. */
    const QString nbook = QStringLiteral("notebook-up-%1").arg(id++);
    mKCal::Notebook *notebook = new mKCal::Notebook(nbook, "test1", "test 1", "red", true, false, false, false, false);
    m_agent->mNotebook = mKCal::Notebook::Ptr(notebook);

    const QString nbuid = QStringLiteral("NBUID:%1:%2").arg(nbook).arg(uid);
    const QString uri = QStringLiteral("/testCal/%1.ics").arg(nbuid);
    const QString etag = QStringLiteral("\"etag-%1\"").arg(uid);

    for (KCalCore::Incidence::List::Iterator it = events.begin();
         it != events.end(); it++) {
        (*it)->setUid(nbuid);
        QVERIFY(m_agent->mCalendar->addEvent(it->staticCast<KCalCore::Event>(), m_agent->mNotebook->uid()));
    }
    m_agent->mStorage->save();
    m_agent->mNotebook->setSyncDate(KDateTime::currentUtcDateTime());

    // Compute delta and check that nothing has changed indeed.
    QVERIFY(m_agent->calculateDelta(remoteUriEtags,
                                    &m_agent->mLocalAdditions,
                                    &m_agent->mLocalModifications,
                                    &m_agent->mLocalDeletions,
                                    &m_agent->mRemoteAdditions,
                                    &m_agent->mRemoteModifications,
                                    &m_agent->mRemoteDeletions));
    QCOMPARE(m_agent->mLocalAdditions.count(), events.count());
    QCOMPARE(m_agent->mLocalModifications.count(), 0);
    QCOMPARE(m_agent->mLocalDeletions.count(), 0);
    QCOMPARE(m_agent->mRemoteAdditions.count(), 0);
    QCOMPARE(m_agent->mRemoteModifications.count(), 0);
    QCOMPARE(m_agent->mRemoteDeletions.count(), 0);

    // Simulate reception of etags for each event.
    remoteUriEtags.insert(uri, etag);
    m_agent->updateHrefETag(nbuid, uri, etag);
    m_agent->mStorage->save();
    m_agent->mNotebook->setSyncDate(KDateTime::currentUtcDateTime());

    // TODO: move these clear statements inside delta ?
    m_agent->mLocalAdditions.clear();
    m_agent->mLocalModifications.clear();
    m_agent->mLocalDeletions.clear();
    m_agent->mRemoteAdditions.clear();
    m_agent->mRemoteModifications.clear();
    m_agent->mRemoteDeletions.clear();
    // Compute delta again and check that nothing has changed indeed.
    QVERIFY(m_agent->calculateDelta(remoteUriEtags,
                                    &m_agent->mLocalAdditions,
                                    &m_agent->mLocalModifications,
                                    &m_agent->mLocalDeletions,
                                    &m_agent->mRemoteAdditions,
                                    &m_agent->mRemoteModifications,
                                    &m_agent->mRemoteDeletions));
    QCOMPARE(m_agent->mLocalAdditions.count(), 0);
    QCOMPARE(m_agent->mLocalModifications.count(), 0);
    QCOMPARE(m_agent->mLocalDeletions.count(), 0);
    QCOMPARE(m_agent->mRemoteAdditions.count(), 0);
    QCOMPARE(m_agent->mRemoteModifications.count(), 0);
    QCOMPARE(m_agent->mRemoteDeletions.count(), 0);

}

void tst_NotebookSyncAgent::updateIncidence_data()
{
    QTest::addColumn<KCalCore::Incidence::Ptr>("incidence");

    {
        KCalCore::Incidence::Ptr ev = KCalCore::Incidence::Ptr(new KCalCore::Event);
        ev->setUid("updateIncidence-111");
        ev->setSummary("Simple added event");
        QTest::newRow("simple added event") << ev;
    }

    {
        KCalCore::Incidence::Ptr ev = KCalCore::Incidence::Ptr(new KCalCore::Event);
        ev->setUid("updateIncidence-111");
        ev->setSummary("Simple updated event");
        QTest::newRow("simple updated event") << ev;
    }

    {
        KCalCore::Incidence::Ptr ev = KCalCore::Incidence::Ptr(new KCalCore::Event);
        ev->setUid("updateIncidence-222");
        ev->setSummary("Recurring added event");
        ev->setDtStart(KDateTime::currentUtcDateTime());
        ev->recurrence()->setDaily(1);
        QTest::newRow("Recurring added event") << ev;

        KCalCore::Incidence::Ptr ex = KCalCore::Incidence::Ptr(ev->clone());
        ex->setSummary("Added exception");
        ex->setRecurrenceId(ev->dtStart().addDays(1));
        QTest::newRow("Added exception") << ex;
    }

    {
        KCalCore::Incidence::Ptr ev = KCalCore::Incidence::Ptr(new KCalCore::Event);
        ev->setUid("updateIncidence-333");
        ev->setSummary("Recurring added orphan");
        ev->setDtStart(KDateTime::currentUtcDateTime());
        ev->setRecurrenceId(ev->dtStart().addDays(1));
        QTest::newRow("Recurring added event") << ev;
    }
}

void tst_NotebookSyncAgent::updateIncidence()
{
    QFETCH(KCalCore::Incidence::Ptr, incidence);

    /* We create a notebook for this test. */
    const QString notebookId = QStringLiteral("26b24ae3-ab05-4892-ac36-632183113e2d");
    mKCal::Notebook::Ptr notebook = m_agent->mStorage->notebook(notebookId);
    if (!notebook) {
        notebook = mKCal::Notebook::Ptr(new mKCal::Notebook(notebookId, "test1", "test 1", "red", true, false, false, false, false));
        m_agent->mStorage->addNotebook(notebook);
    }
    m_agent->mNotebook = notebook;

    bool critical;
    const bool orphan = incidence->summary().contains(QStringLiteral("orphan"));
    m_agent->mCalendar->addNotebook(notebook->uid(), true);
    m_agent->mCalendar->setDefaultNotebook(notebook->uid());
    QVERIFY(m_agent->updateIncidence(incidence, QStringLiteral("uri.ics"),
                                     QStringLiteral("etag"), orphan, &critical));

    KCalCore::Incidence::Ptr fetched =
        m_agent->mCalendar->incidence(incidence->uid(), incidence->recurrenceId());
    QVERIFY(IncidenceHandler::copiedPropertiesAreEqual(incidence, fetched));
}

#include "tst_notebooksyncagent.moc"
QTEST_MAIN(tst_NotebookSyncAgent)
