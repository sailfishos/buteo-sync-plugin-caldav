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
#include "report.h"
#include "put.h"

#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/Event>
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

    void updateEvent();
    void updateHrefETag();
    void calculateDelta();

    void oneDownSyncCycle_data();
    void oneDownSyncCycle();

    void oneUpSyncCycle_data();
    void oneUpSyncCycle();

    void updateIncidence_data();
    void updateIncidence();

    void requestFinished();

    void result();

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
    mKCal::ExtendedCalendar::Ptr cal = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar( QByteArray( "UTC" ) ));
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
    QTest::newRow("insert an exception at an EXDATE")
        << "data/notebooksyncagent_insert_exdate.xml"
        << QStringLiteral("NBUID:123456789:9e3e9495-7fca-46b1-9ae4-207f3a1a9148")
        << QStringLiteral("EXAMPLE")
        << QStringLiteral("2019-07-24")
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

    KCalendarCore::Incidence::Ptr ev;
    if (expectedRecurrenceID.isEmpty())
        ev = m_agent->mCalendar->event(expectedUID);
    else
        ev = m_agent->mCalendar->event(expectedUID, QDateTime::fromString(expectedRecurrenceID, Qt::ISODate));
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
                          << QStringLiteral("2017-03-23T11:00:00Z")
                          << QString()
                          << QStringLiteral("2017-03-30T11:00:00Z"));
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
    KCalendarCore::Incidence::List incidences = m_agent->mCalendar->incidences();
    for (int i = 0; i < expectedUIDs.size(); ++i) {
        KCalendarCore::Incidence::Ptr ev;
        if (expectedRecurrenceIDs[i].isEmpty()) {
            ev = m_agent->mCalendar->event(expectedUIDs[i]);
        } else {
            QDateTime recId = QDateTime::fromString(expectedRecurrenceIDs[i], Qt::ISODate);
            ev = m_agent->mCalendar->event(expectedUIDs[i], recId);
        }
        qWarning() << "Trying to find event:" << expectedUIDs[i] << expectedRecurrenceIDs[i];
        QVERIFY(ev);
        QCOMPARE(ev->uid(), expectedUIDs[i]);
        QCOMPARE(ev->summary(), expectedSummaries[i]);
    }
}

static QString fetchUri(KCalendarCore::Incidence::Ptr incidence)
{
    Q_FOREACH (const QString &comment, incidence->comments()) {
        if (comment.startsWith("buteo:caldav:uri:")) {
            QString uri = comment.mid(17);
            return uri;
        }
    }
    return QString();
}
static QString fetchETag(KCalendarCore::Incidence::Ptr incidence)
{
    const QStringList &comments(incidence->comments());
    Q_FOREACH (const QString &comment, comments) {
        if (comment.startsWith("buteo:caldav:etag:")) {
            return comment.mid(18);
        }
    }
    return QString();
}

void tst_NotebookSyncAgent::updateEvent()
{
    // Populate the database.
    KCalendarCore::Incidence::Ptr incidence = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    incidence->setUid("123456-moz");
    incidence->setNonKDECustomProperty("X-MOZ-LASTACK", "20171013T174424Z");
    incidence->setCreated(QDateTime(QDate(2019, 03, 28), QTime(), Qt::UTC));
    QCOMPARE(incidence->created().date(), QDate(2019, 03, 28));

    QVERIFY(m_agent->mCalendar->addEvent(incidence.staticCast<KCalendarCore::Event>(),
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
    KCalendarCore::Incidence::Ptr update = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    update->setUid("123456-moz");
    update->setNonKDECustomProperty("X-MOZ-LASTACK", "20171016T174424Z");
    update->setAllDay(false);
    update->addComment(QStringLiteral("buteo:caldav:uri:plop.ics"));
    m_agent->updateIncidence(update, incidence);

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
    KCalendarCore::Incidence::Ptr incidence = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    incidence->setUid("123456-single");
    m_agent->mCalendar->addEvent(incidence.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    incidence = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    incidence->setUid("123456-recurs");
    incidence->setDtStart(QDateTime::currentDateTimeUtc());
    incidence->recurrence()->setDaily(1);
    incidence->recurrence()->setDuration(28);
    m_agent->mCalendar->addEvent(incidence.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    QDateTime refId = incidence->recurrence()->getNextDateTime(incidence->dtStart().addDays(4));
    incidence = m_agent->mCalendar->dissociateSingleOccurrence(incidence, refId);
    m_agent->mCalendar->addEvent(incidence.staticCast<KCalendarCore::Event>(),
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

static bool incidenceListContains(const KCalendarCore::Incidence::List &list,
                                  const KCalendarCore::Incidence::Ptr &ev)
{
    for (KCalendarCore::Incidence::List::ConstIterator it = list.constBegin();
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
    QDateTime cur = QDateTime::currentDateTimeUtc();

    // Populate the database.
    KCalendarCore::Incidence::Ptr ev222 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev222->setSummary("local modification");
    ev222->addComment(QStringLiteral("buteo:caldav:uri:%1222.ics").arg(m_agent->mRemoteCalendarPath));
    ev222->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag222"));
    m_agent->mCalendar->addEvent(ev222.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalendarCore::Incidence::Ptr ev333 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev333->setSummary("local deletion");
    ev333->addComment(QStringLiteral("buteo:caldav:uri:%1333.ics").arg(m_agent->mRemoteCalendarPath));
    ev333->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag333"));
    m_agent->mCalendar->addEvent(ev333.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalendarCore::Incidence::Ptr ev444 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev444->addComment(QStringLiteral("buteo:caldav:uri:%1444.ics").arg(m_agent->mRemoteCalendarPath));
    ev444->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag444"));
    ev444->setSummary("local modification discarded by a remote modification");
    m_agent->mCalendar->addEvent(ev444.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalendarCore::Incidence::Ptr ev555 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev555->addComment(QStringLiteral("buteo:caldav:uri:%1555.ics").arg(m_agent->mRemoteCalendarPath));
    ev555->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag555"));
    ev555->setSummary("local modification discarded by a remote deletion");
    ev555->setDtStart(cur);
    m_agent->mCalendar->addEvent(ev555.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalendarCore::Incidence::Ptr ev666 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev666->addComment(QStringLiteral("buteo:caldav:uri:%1666.ics").arg(m_agent->mRemoteCalendarPath));
    ev666->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag666"));
    ev666->setSummary("remote modification");
    m_agent->mCalendar->addEvent(ev666.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalendarCore::Incidence::Ptr ev777 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev777->addComment(QStringLiteral("buteo:caldav:uri:%1777.ics").arg(m_agent->mRemoteCalendarPath));
    ev777->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag777"));
    ev777->setSummary("remote deletion");
    ev777->setDtStart(cur.addDays(-1));
    ev777.staticCast<KCalendarCore::Event>()->setDtEnd(cur.addDays(1));
    m_agent->mCalendar->addEvent(ev777.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalendarCore::Incidence::Ptr ev888 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev888->setRecurrenceId(QDateTime());
    ev888->addComment(QStringLiteral("buteo:caldav:uri:%1888.ics").arg(m_agent->mRemoteCalendarPath));
    ev888->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag888"));
    ev888->setSummary("unchanged synced incidence");
    QDateTime recId = QDateTime::currentDateTimeUtc();
    ev888->setDtStart( recId );
    ev888->recurrence()->addRDateTime(recId);
    m_agent->mCalendar->addEvent(ev888.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalendarCore::Incidence::Ptr ev112 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev112->setSummary("partial local addition, need download");
    m_agent->mCalendar->addEvent(ev112.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalendarCore::Incidence::Ptr ev113 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev113->setSummary("partial local modification, need upload");
    m_agent->mCalendar->addEvent(ev113.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    KCalendarCore::Incidence::Ptr ev001 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev001->setSummary("shared event, out-side sync window");
    ev001->setDtStart(cur.addDays(-7));
    ev001->addComment(QStringLiteral("buteo:caldav:uri:%1001.ics").arg(m_agent->mRemoteCalendarPath));
    ev001->addComment(QStringLiteral("buteo:caldav:etag:\"%1\"").arg("etag001"));
    m_agent->mCalendar->addEvent(ev001.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());

    m_agent->mStorage->save();
    QDateTime lastSync = QDateTime::currentDateTimeUtc();
    m_agent->mNotebook->setSyncDate(lastSync.addSecs(1));

    // Sleep a bit to ensure that modification done after the sleep will have
    // dates that are later than creation ones, so inquiring the local database
    // with modifications strictly later than lastSync value will actually
    // returned the right events and not all.
    QThread::sleep(3);

    // Perform local modifications.
    KCalendarCore::Incidence::Ptr ev111 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev111->setSummary("local addition");
    ev113->setDescription(QStringLiteral("Modified summary."));
    m_agent->mCalendar->addEvent(ev111.staticCast<KCalendarCore::Event>(),
                                 m_agent->mNotebook->uid());
    ev222->setDescription(QStringLiteral("Modified summary."));
    m_agent->mCalendar->deleteIncidence(ev333);
    ev444->setDescription(QStringLiteral("Modified summary."));
    ev555->setDescription(QStringLiteral("Modified summary."));
    KCalendarCore::Incidence::Ptr ev999 = m_agent->mCalendar->dissociateSingleOccurrence(ev888, recId);
    QVERIFY(ev999);
    ev999->setSummary("local addition of persistent exception");
    m_agent->mCalendar->addEvent(ev999.staticCast<KCalendarCore::Event>(),
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

    // Create the sync window by hand.
    m_agent->mFromDateTime = cur.addSecs(-1);
    m_agent->mToDateTime = cur.addSecs(30);
    QVERIFY(m_agent->calculateDelta(remoteUriEtags,
                                    &m_agent->mLocalAdditions,
                                    &m_agent->mLocalModifications,
                                    &m_agent->mLocalDeletions,
                                    &m_agent->mRemoteChanges,
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
    QCOMPARE(m_agent->mRemoteChanges.count(), 4);
    QVERIFY(m_agent->mRemoteChanges.contains
            (QStringLiteral("%1000.ics").arg(m_agent->mRemoteCalendarPath)));
    QVERIFY(m_agent->mRemoteChanges.contains
            (QStringLiteral("%1%2.ics").arg(m_agent->mRemoteCalendarPath).arg(ev112->uid())));
    QVERIFY(m_agent->mRemoteChanges.contains
            (QStringLiteral("%1444.ics").arg(m_agent->mRemoteCalendarPath)));
    QVERIFY(m_agent->mRemoteChanges.contains
            (QStringLiteral("%1666.ics").arg(m_agent->mRemoteCalendarPath)));
    uint nFound = 0, nNotFound = 0;
    Q_FOREACH(const KCalendarCore::Incidence::Ptr &incidence, m_agent->mRemoteDeletions) {
        if (incidence->uid() == ev555->uid()
            || incidence->uid() == ev777->uid()) {
            nFound += 1;
        }
        if (incidence->uid() == ev001->uid()) {
            nNotFound += 1;
        }
    }
    QCOMPARE(nFound, uint(2));
    QCOMPARE(nNotFound, uint(0));
}

Q_DECLARE_METATYPE(KCalendarCore::Incidence::Ptr)
void tst_NotebookSyncAgent::oneDownSyncCycle_data()
{
    QTest::addColumn<QString>("notebookId");
    QTest::addColumn<QString>("uid");
    QTest::addColumn<KCalendarCore::Incidence::List>("events");
    KCalendarCore::Incidence::Ptr ev;

    ev = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev->setSummary("Simple event");
    ev->setCreated(QDateTime::currentDateTimeUtc().addDays(-1));
    QTest::newRow("simple event")
        << QStringLiteral("notebook-down-1")
        << QStringLiteral("111") << (KCalendarCore::Incidence::List() << ev);

    ev = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev->setSummary("Recurent event");
    ev->setDtStart(QDateTime::currentDateTimeUtc());
    ev->recurrence()->setDaily(1);
    ev->recurrence()->setDuration(28);
    QDateTime refId = ev->recurrence()->getNextDateTime(ev->dtStart().addDays(4));
    KCalendarCore::Incidence::Ptr ex =
        m_agent->mCalendar->dissociateSingleOccurrence(ev, refId);
    ex->setSummary("Persistent exception");
    QTest::newRow("recurent event with exception")
        << QStringLiteral("notebook-down-2")
        << QStringLiteral("222") << (KCalendarCore::Incidence::List() << ev << ex);

    refId = ev->recurrence()->getNextDateTime(ev->dtStart().addDays(2));
    ex = m_agent->mCalendar->dissociateSingleOccurrence(ev, refId);
    ex->setSummary("orphan event");
    QTest::newRow("orphan persistent exception event")
        << QStringLiteral("notebook-down-3")
        << QStringLiteral("333") << (KCalendarCore::Incidence::List() << ex);

    ex->setSummary("modified persistent exception event");
    QTest::newRow("modified persistent exception event")
        << QStringLiteral("notebook-down-3")
        << QStringLiteral("333") << (KCalendarCore::Incidence::List() << ex);
}

void tst_NotebookSyncAgent::oneDownSyncCycle()
{
    QFETCH(QString, notebookId);
    QFETCH(QString, uid);
    QFETCH(KCalendarCore::Incidence::List, events);
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
    KCalendarCore::MemoryCalendar::Ptr memoryCalendar(new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    for (KCalendarCore::Incidence::List::Iterator it = events.begin();
         it != events.end(); it++) {
        (*it)->setUid(uid);
        if ((*it)->recurs()) {
            Q_FOREACH (KCalendarCore::Incidence::Ptr instance, events) {
                KCalendarCore::DateTimeList exDateTimes =
                    (*it)->recurrence()->exDateTimes();
                exDateTimes.removeAll(instance->recurrenceId());
                (*it)->recurrence()->setExDateTimes(exDateTimes);
            }
        }
        memoryCalendar->addIncidence(*it);
    }

    // The sync date is the one at the start of the process. Due to network
    // latencies, the actual updateIncidences() call may happen some seconds
    // after the stored sync date. We simulate this here with a -2 time shift.
    m_agent->mNotebookSyncedDateTime = QDateTime::currentDateTimeUtc().addSecs(-2);

    KCalendarCore::ICalFormat icalFormat;
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
    m_agent->mNotebook->setSyncDate(m_agent->mNotebookSyncedDateTime);

    // Compute delta and check that nothing has changed indeed.
    QVERIFY(m_agent->calculateDelta(remoteUriEtags,
                                    &m_agent->mLocalAdditions,
                                    &m_agent->mLocalModifications,
                                    &m_agent->mLocalDeletions,
                                    &m_agent->mRemoteChanges,
                                    &m_agent->mRemoteDeletions));
    QCOMPARE(m_agent->mLocalAdditions.count(), 0);
    QCOMPARE(m_agent->mLocalModifications.count(), 0);
    QCOMPARE(m_agent->mLocalDeletions.count(), 0);
    QCOMPARE(m_agent->mRemoteChanges.count(), 0);
    QCOMPARE(m_agent->mRemoteDeletions.count(), 0);
}

void tst_NotebookSyncAgent::oneUpSyncCycle_data()
{
    QTest::addColumn<QString>("uid");
    QTest::addColumn<KCalendarCore::Incidence::List>("events");
    KCalendarCore::Incidence::Ptr ev;

    ev = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev->setSummary("Simple added event");
    QTest::newRow("simple added event")
        << QStringLiteral("100")
        << (KCalendarCore::Incidence::List() << ev);

    ev = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    ev->setSummary("Recurent event");
    ev->setDtStart(QDateTime::currentDateTimeUtc());
    ev->recurrence()->setDaily(1);
    ev->recurrence()->setDuration(28);
    QDateTime refId = ev->recurrence()->getNextDateTime(ev->dtStart().addDays(4));
    KCalendarCore::Incidence::Ptr ex =
        m_agent->mCalendar->dissociateSingleOccurrence(ev, refId);
    ex->setSummary("Persistent exception");
    QTest::newRow("added recurent event with exception")
        << QStringLiteral("200")
        << (KCalendarCore::Incidence::List() << ev << ex);
}

void tst_NotebookSyncAgent::oneUpSyncCycle()
{
    QFETCH(QString, uid);
    QFETCH(KCalendarCore::Incidence::List, events);
    QHash<QString, QString> remoteUriEtags;
    static int id = 0;

    /* We create a notebook for this test. */
    const QString nbook = QStringLiteral("notebook-up-%1").arg(id++);
    mKCal::Notebook *notebook = new mKCal::Notebook(nbook, "test1", "test 1", "red", true, false, false, false, false);
    m_agent->mNotebook = mKCal::Notebook::Ptr(notebook);

    const QString nbuid = QStringLiteral("NBUID:%1:%2").arg(nbook).arg(uid);
    const QString uri = QStringLiteral("/testCal/%1.ics").arg(nbuid);
    const QString etag = QStringLiteral("\"etag-%1\"").arg(uid);

    for (KCalendarCore::Incidence::List::Iterator it = events.begin();
         it != events.end(); it++) {
        (*it)->setUid(nbuid);
        QVERIFY(m_agent->mCalendar->addEvent(it->staticCast<KCalendarCore::Event>(), m_agent->mNotebook->uid()));
    }
    m_agent->mStorage->save();
    m_agent->mNotebook->setSyncDate(QDateTime::currentDateTimeUtc());

    // Compute delta and check that nothing has changed indeed.
    QVERIFY(m_agent->calculateDelta(remoteUriEtags,
                                    &m_agent->mLocalAdditions,
                                    &m_agent->mLocalModifications,
                                    &m_agent->mLocalDeletions,
                                    &m_agent->mRemoteChanges,
                                    &m_agent->mRemoteDeletions));
    QCOMPARE(m_agent->mLocalAdditions.count(), events.count());
    QCOMPARE(m_agent->mLocalModifications.count(), 0);
    QCOMPARE(m_agent->mLocalDeletions.count(), 0);
    QCOMPARE(m_agent->mRemoteChanges.count(), 0);
    QCOMPARE(m_agent->mRemoteDeletions.count(), 0);

    // Simulate reception of etags for each event.
    remoteUriEtags.insert(uri, etag);
    m_agent->updateHrefETag(nbuid, uri, etag);
    m_agent->mStorage->save();
    m_agent->mNotebook->setSyncDate(QDateTime::currentDateTimeUtc());

    // TODO: move these clear statements inside delta ?
    m_agent->mLocalAdditions.clear();
    m_agent->mLocalModifications.clear();
    m_agent->mLocalDeletions.clear();
    m_agent->mRemoteChanges.clear();
    m_agent->mRemoteDeletions.clear();
    // Compute delta again and check that nothing has changed indeed.
    QVERIFY(m_agent->calculateDelta(remoteUriEtags,
                                    &m_agent->mLocalAdditions,
                                    &m_agent->mLocalModifications,
                                    &m_agent->mLocalDeletions,
                                    &m_agent->mRemoteChanges,
                                    &m_agent->mRemoteDeletions));
    QCOMPARE(m_agent->mLocalAdditions.count(), 0);
    QCOMPARE(m_agent->mLocalModifications.count(), 0);
    QCOMPARE(m_agent->mLocalDeletions.count(), 0);
    QCOMPARE(m_agent->mRemoteChanges.count(), 0);
    QCOMPARE(m_agent->mRemoteDeletions.count(), 0);

}

void tst_NotebookSyncAgent::updateIncidence_data()
{
    QTest::addColumn<KCalendarCore::Incidence::Ptr>("incidence");

    {
        KCalendarCore::Incidence::Ptr ev = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
        ev->setUid("updateIncidence-111");
        ev->setSummary("Simple added event");
        QTest::newRow("simple added event") << ev;
    }

    {
        KCalendarCore::Incidence::Ptr ev = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
        ev->setUid("updateIncidence-111");
        ev->setSummary("Simple updated event");
        QTest::newRow("simple updated event") << ev;
    }

    {
        KCalendarCore::Incidence::Ptr ev = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
        ev->setUid("updateIncidence-222");
        ev->setSummary("Recurring added event");
        ev->setDtStart(QDateTime::currentDateTimeUtc());
        ev->recurrence()->setDaily(1);
        QTest::newRow("Recurring added event") << ev;

        KCalendarCore::Incidence::Ptr ex = KCalendarCore::Incidence::Ptr(ev->clone());
        ex->setSummary("Added exception");
        ex->setRecurrenceId(ev->dtStart().addDays(1));
        ex->clearRecurrence();
        QTest::newRow("Added exception") << ex;
    }

    {
        KCalendarCore::Incidence::Ptr ev = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
        ev->setUid("updateIncidence-333");
        ev->setSummary("Recurring added orphan");
        ev->setDtStart(QDateTime::currentDateTimeUtc());
        ev->setRecurrenceId(ev->dtStart().addDays(1));
        QTest::newRow("Recurring added event") << ev;
    }

    {
        KCalendarCore::Incidence::Ptr ev = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
        ev->setUid("updateIncidence-444");
        ev->setSummary("Recurring added event with an EXDATE");
        ev->setDtStart(QDateTime::currentDateTimeUtc());
        ev->recurrence()->setDaily(1);
        ev->recurrence()->addExDateTime(ev->dtStart().addDays(1));
        QTest::newRow("Recurring added event with an EXDATE") << ev;

        // Normally, one should not add an exception at an exdate,
        // but some faulty servers are sending recurring events with exdates
        // and exceptions during these exdates. Since, the exception
        // is actually known, it's better for the user to relax
        // this case and add the exception anyway.
        KCalendarCore::Incidence::Ptr ex = KCalendarCore::Incidence::Ptr(ev->clone());
        ex->setSummary("Added exception at a faulty EXDATE");
        ex->setRecurrenceId(ev->dtStart().addDays(1));
        ex->clearRecurrence();
        QTest::newRow("Added exception at a faulty EXDATE") << ex;
    }
}

void tst_NotebookSyncAgent::updateIncidence()
{
    QFETCH(KCalendarCore::Incidence::Ptr, incidence);

    /* We create a notebook for this test. */
    const QString notebookId = QStringLiteral("26b24ae3-ab05-4892-ac36-632183113e2d");
    mKCal::Notebook::Ptr notebook = m_agent->mStorage->notebook(notebookId);
    if (!notebook) {
        notebook = mKCal::Notebook::Ptr(new mKCal::Notebook(notebookId, "test1", "test 1", "red", true, false, false, false, false));
        m_agent->mStorage->addNotebook(notebook);
    }
    m_agent->mNotebook = notebook;

    Reader::CalendarResource resource;
    resource.href = QStringLiteral("uri.ics");
    resource.etag = QStringLiteral("etag");
    resource.incidences << incidence;
    QVERIFY(m_agent->updateIncidences(QList<Reader::CalendarResource>() << resource));

    KCalendarCore::Incidence::Ptr fetched =
        m_agent->mCalendar->incidence(incidence->uid(), incidence->recurrenceId());
    // Created date may differ on dissociated occurrences, artificially set it.
    incidence->setCreated(fetched->created());
    // Fetched will have an added end date because of dissociateSingleOccurrence()
    if (fetched->type() == KCalendarCore::Incidence::TypeEvent
        && fetched.staticCast<KCalendarCore::Event>()->hasEndDate()) {
        incidence.staticCast<KCalendarCore::Event>()->setDtEnd(fetched.staticCast<KCalendarCore::Event>()->dtEnd());
    }
    QCOMPARE(*incidence, *fetched);
}

void tst_NotebookSyncAgent::requestFinished()
{
    QSignalSpy finished(m_agent, &NotebookSyncAgent::finished);

    Report *report = new Report(m_agent->mNetworkManager, m_agent->mSettings);
    m_agent->mRequests.insert(report);
    QCOMPARE(m_agent->mRequests.count(), 1);

    m_agent->requestFinished(report);
    QCOMPARE(m_agent->mRequests.count(), 0);
    QCOMPARE(finished.count(), 1);
    finished.clear();

    Put *put1 = new Put(m_agent->mNetworkManager, m_agent->mSettings);
    m_agent->mRequests.insert(put1);
    QCOMPARE(m_agent->mRequests.count(), 1);

    Put *put2 = new Put(m_agent->mNetworkManager, m_agent->mSettings);
    m_agent->mRequests.insert(put2);
    QCOMPARE(m_agent->mRequests.count(), 2);

    m_agent->requestFinished(put2);
    QCOMPARE(m_agent->mRequests.count(), 1);
    QCOMPARE(finished.count(), 0);
    QVERIFY(m_agent->mRequests.contains(put1));

    m_agent->requestFinished(put1);
    QCOMPARE(m_agent->mRequests.count(), 0);
    QCOMPARE(finished.count(), 1);
}

void tst_NotebookSyncAgent::result()
{
    m_agent->mSyncMode = NotebookSyncAgent::QuickSync;

    KCalendarCore::Incidence::Ptr rAdd1 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rAdd1->addComment(QStringLiteral("buteo:caldav:uri:/path/event1"));
    KCalendarCore::Incidence::Ptr rAdd2 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rAdd2->addComment(QStringLiteral("buteo:caldav:uri:/path/event2"));
    KCalendarCore::Incidence::Ptr rAdd3 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rAdd3->addComment(QStringLiteral("buteo:caldav:uri:/path/event3"));
    m_agent->mRemoteAdditions = KCalendarCore::Incidence::List()
        << rAdd1 << rAdd2 << rAdd3;
    KCalendarCore::Incidence::Ptr rDel1 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rDel1->addComment(QStringLiteral("buteo:caldav:uri:/path/event01"));
    KCalendarCore::Incidence::Ptr rDel2 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rDel2->addComment(QStringLiteral("buteo:caldav:uri:/path/event02"));
    KCalendarCore::Incidence::Ptr rDel3 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rDel3->addComment(QStringLiteral("buteo:caldav:uri:/path/event03"));
    KCalendarCore::Incidence::Ptr rDel4 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rDel4->addComment(QStringLiteral("buteo:caldav:uri:/path/event04"));
    m_agent->mRemoteDeletions = KCalendarCore::Incidence::List()
        << rDel1 << rDel2 << rDel3 << rDel4;
    KCalendarCore::Incidence::Ptr rMod1 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rMod1->addComment(QStringLiteral("buteo:caldav:uri:/path/event11"));
    KCalendarCore::Incidence::Ptr rMod2 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rMod2->addComment(QStringLiteral("buteo:caldav:uri:/path/event12"));
    KCalendarCore::Incidence::Ptr rMod3 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rMod3->addComment(QStringLiteral("buteo:caldav:uri:/path/event13"));
    KCalendarCore::Incidence::Ptr rMod4 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rMod4->addComment(QStringLiteral("buteo:caldav:uri:/path/event14"));
    KCalendarCore::Incidence::Ptr rMod5 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    rMod5->addComment(QStringLiteral("buteo:caldav:uri:/path/event15"));
    m_agent->mRemoteModifications = KCalendarCore::Incidence::List()
        << rMod1 << rMod2 << rMod3 << rMod4 << rMod5;

    KCalendarCore::Incidence::Ptr lAdd1 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    lAdd1->setUid("event001");
    KCalendarCore::Incidence::Ptr lAdd2 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    lAdd2->setUid("event002");
    m_agent->mLocalAdditions = KCalendarCore::Incidence::List() << lAdd1 << lAdd2;
    KCalendarCore::Incidence::Ptr lDel1 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    lDel1->addComment(QStringLiteral("buteo:caldav:uri:/path/event101"));
    KCalendarCore::Incidence::Ptr lDel2 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    lDel2->addComment(QStringLiteral("buteo:caldav:uri:/path/event102"));
    KCalendarCore::Incidence::Ptr lDel3 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    lDel3->addComment(QStringLiteral("buteo:caldav:uri:/path/event103"));
    m_agent->mLocalDeletions = KCalendarCore::Incidence::List() << lDel1 << lDel2 << lDel3;
    KCalendarCore::Incidence::Ptr lMod1 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    lMod1->addComment(QStringLiteral("buteo:caldav:uri:/path/event111"));
    KCalendarCore::Incidence::Ptr lMod2 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    lMod2->addComment(QStringLiteral("buteo:caldav:uri:/path/event112"));
    KCalendarCore::Incidence::Ptr lMod3 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    lMod3->addComment(QStringLiteral("buteo:caldav:uri:/path/event113"));
    KCalendarCore::Incidence::Ptr lMod4 = KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    lMod4->addComment(QStringLiteral("buteo:caldav:uri:/path/event114"));
    m_agent->mLocalModifications = KCalendarCore::Incidence::List()
        << lMod1 << lMod2 << lMod3 << lMod4;

    m_agent->mFailingUpdates = QSet<QString>()
        << "/path/event1" << "/path/event01" << "/path/event11";
    m_agent->mFailingUploads = QSet<QString>()
        << "/testCal/event001.ics" << "/path/event101" << "/path/event111";

    Buteo::TargetResults results = m_agent->result();
    QCOMPARE(results.targetName(), QLatin1String("test1"));

    QCOMPARE(results.remoteItems().added, unsigned(1));
    QCOMPARE(results.remoteItems().deleted, unsigned(2));
    QCOMPARE(results.remoteItems().modified, unsigned(3));

    QCOMPARE(results.localItems().added, unsigned(2));
    QCOMPARE(results.localItems().deleted, unsigned(3));
    QCOMPARE(results.localItems().modified, unsigned(4));

    m_agent->mSyncMode = NotebookSyncAgent::SlowSync;

    Reader::CalendarResource r1;
    r1.href = "/path/event1";
    r1.incidences << KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    Reader::CalendarResource r2;
    r2.href = "/path/event2";
    r2.incidences << KCalendarCore::Incidence::Ptr(new KCalendarCore::Event);
    m_agent->mReceivedCalendarResources = QList<Reader::CalendarResource>() << r1 << r2;

    results = m_agent->result();
    QCOMPARE(results.targetName(), QLatin1String("test1"));

    QCOMPARE(results.remoteItems().added, unsigned(0));
    QCOMPARE(results.remoteItems().deleted, unsigned(0));
    QCOMPARE(results.remoteItems().modified, unsigned(0));

    QCOMPARE(results.localItems().added, unsigned(1));
    QCOMPARE(results.localItems().deleted, unsigned(0));
    QCOMPARE(results.localItems().modified, unsigned(0));

}

#include "tst_notebooksyncagent.moc"
QTEST_MAIN(tst_NotebookSyncAgent)
