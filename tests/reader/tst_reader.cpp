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
#include <QFile>

#include <reader.h>

class tst_Reader : public QObject
{
    Q_OBJECT

public:
    tst_Reader();
    virtual ~tst_Reader();

public slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

private slots:
    void readICal_data();
    void readICal();
};

tst_Reader::tst_Reader()
{
}

tst_Reader::~tst_Reader()
{
}

void tst_Reader::initTestCase()
{
    qputenv("MSYNCD_LOGGING_LEVEL", "8");
}

void tst_Reader::cleanupTestCase()
{    
}

void tst_Reader::init()
{
}

void tst_Reader::cleanup()
{
}

void tst_Reader::readICal_data()
{
    QTest::addColumn<QString>("xmlFilename");
    QTest::addColumn<int>("expectedNResponses");
    QTest::addColumn<int>("expectedNIncidences");
    QTest::addColumn<QString>("expectedUID");
    QTest::addColumn<QString>("expectedSummary");
    QTest::addColumn<QString>("expectedDescription");
    QTest::addColumn<bool>("expectedRecurs");
    QTest::addColumn<int>("expectedNAlarms");

    QTest::newRow("no incidence response")
        << QStringLiteral("data/reader_noevent.xml")
        << 1
        << 0
        << QString()
        << QString()
        << QString()
        << false
        << 0;
    QTest::newRow("basic one incidence response")
        << QStringLiteral("data/reader_base.xml")
        << 1
        << 1
        << QStringLiteral("972a7c13-bbd6-4fce-9ebb-03a808111828")
        << QStringLiteral("Test")
        << QStringLiteral("")
        << false
        << 1;
    QTest::newRow("missing version response")
        << QStringLiteral("data/reader_missing.xml")
        << 1
        << 1
        << QStringLiteral("972a7c13-bbd6-4fce-9ebb-03a808111828")
        << QStringLiteral("Test")
        << QStringLiteral("")
        << false
        << 0;
    QTest::newRow("UTF8 description response")
        << QStringLiteral("data/reader_UTF8_description.xml")
        << 1
        << 1
        << QStringLiteral("123456789")
        << QStringLiteral("Sieger F - Zweiter E")
        << QStringLiteral("UTF8 characters: nœud")
        << false
        << 0;
    QTest::newRow("funny character in UID response")
        << QStringLiteral("data/reader_UID.xml")
        << 1
        << 1
        << QStringLiteral("123-456@789%369$258*147")
        << QStringLiteral("Sieger F - Zweiter E")
        << QStringLiteral("description")
        << false
        << 0;
    QTest::newRow("description with CR response")
        << QStringLiteral("data/reader_CR_description.xml")
        << 1
        << 1
        << QStringLiteral("123-456@789%369$258*147")
        << QStringLiteral("Sieger F - Zweiter E")
        << QStringLiteral("description\nmultilines\n")
        << false
        << 0;
}

void tst_Reader::readICal()
{
    QFETCH(QString, xmlFilename);
    QFETCH(int, expectedNResponses);
    QFETCH(int, expectedNIncidences);
    QFETCH(QString, expectedUID);
    QFETCH(QString, expectedSummary);
    QFETCH(QString, expectedDescription);
    QFETCH(bool, expectedRecurs);
    QFETCH(int, expectedNAlarms);

    QFile f(QStringLiteral("%1/%2").arg(QCoreApplication::applicationDirPath(), xmlFilename));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        QFAIL("Data file does not exist or cannot be opened for reading!");
    }

    Reader rd;
    rd.read(f.readAll());

    QCOMPARE(rd.results().size(), expectedNResponses);
    QCOMPARE(rd.results().values()[0].incidences.length(), expectedNIncidences);

    if (!expectedNIncidences)
        return;
    KCalCore::Incidence::Ptr ev = KCalCore::Incidence::Ptr(rd.results().values()[0].incidences[0]);
    
    QCOMPARE(ev->uid(), expectedUID);
    QCOMPARE(ev->summary(), expectedSummary);
    QCOMPARE(ev->description(), expectedDescription);

    QCOMPARE(ev->recurs(), expectedRecurs);
    QCOMPARE(ev->alarms().length(), expectedNAlarms);
}

#include "tst_reader.moc"
QTEST_MAIN(tst_Reader)
