/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2013 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Mani Chandrasekar <maninc@gmail.com>
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
 *
 */

#include "reader_p.h"
#include "logging_p.h"

#include <QDebug>
#include <QUrl>
#include <QList>
#include <QRegExp>
#include <QByteArray>
#include <QXmlStreamReader>

namespace {
   /* Some servers don't XML-escape the ics content when they return it
    * in the XML stream, so we need to fix any issues.
    * Note that this can cause line-lengths to exceed the spec (due to
    * & -> &amp; expansion etc) but our iCal parser is more robust than
    * our XML parser, so this works. */
    QByteArray xmlSanitiseIcsData(const QByteArray &data) {
        QList<QByteArray> lines = data.split('\n');
        int depth = 0;
        bool inCData = false;
        QByteArray retn;
        retn.reserve(data.size());
        for (QList<QByteArray>::const_iterator it = lines.constBegin(); it != lines.constEnd(); it++) {
            QByteArray line = *it;
            if (line.contains("BEGIN:VCALENDAR")) {
                depth += 1;
                inCData = line.contains("<![CDATA[");
            } else if (line.contains("END:VCALENDAR")) {
                depth -= 1;
                inCData = false;
            } else if (depth > 0 && !inCData) {
                // We're inside a VCALENDAR/ics block.
                // First, hack to turn sanitised input into malformed input:
                line.replace("&amp;",  "&");
                line.replace("&quot;", "\"");
                line.replace("&apos;", "'");
                line.replace("&lt;",   "<");
                line.replace("&gt;",   ">");
                // Then, fix for malformed input:
                QString lineStr(line);
                // RegExp should avoid escaping & when this character is starting
                // a valid numeric character reference (decimal or hexadecimal).
                // Other HTLML entities like &nbsp; seems to make iCal parser
                // fails, so we're encoding them.
                lineStr.replace(QRegExp("&(?!#[0-9]+;|#x[0-9A-Fa-f]+;)"), "&amp;");
                line = lineStr.toUtf8();
                line.replace('"',  "&quot;");
                line.replace('\'', "&apos;");
                line.replace('<',  "&lt;");
                line.replace('>',  "&gt;");
            }
            retn.append(line);
            retn.append('\n');
        }
        return retn;
    }
}

Reader::Reader(QObject *parent)
    : QObject(parent)
{
}

Reader::~Reader()
{
    delete mReader;
}

void Reader::read(const QByteArray &data)
{
    delete mReader;
    mReader = new QXmlStreamReader(xmlSanitiseIcsData(data));
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "multistatus") {
            mValidResponse = true;
            readMultiStatus();
        }
    }
}

bool Reader::hasError() const
{
    if (!mReader)
        return false;

    return !mValidResponse;
}

const QList<Buteo::Dav::Resource>& Reader::results() const
{
    return mResults;
}

void Reader::readMultiStatus()
{
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "response") {
            readResponse();
        } else {
            mReader->skipCurrentElement();
        }
    }
}

void Reader::readResponse()
{
    Buteo::Dav::Resource resource;
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "href") {
            resource.href = QUrl::fromPercentEncoding(mReader->readElementText().toLatin1());
        } else if (mReader->name() == "propstat") {
            readPropStat(&resource);
        } else {
            mReader->skipCurrentElement();
        }
    }
    if (resource.href.isEmpty()) {
        qCWarning(lcDav) << "Ignoring received calendar object data, is missing href value";
        return;
    }

    mResults.append(resource);
}

void Reader::readPropStat(Buteo::Dav::Resource *resource)
{
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "prop") {
            readProp(resource);
        } else if (mReader->name() == "status") {
            resource->status = mReader->readElementText();
        } else {
            mReader->skipCurrentElement();
        }
    }
}

void Reader::readProp(Buteo::Dav::Resource *resource)
{
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "getetag") {
            resource->etag = mReader->readElementText();
        } else if (mReader->name() == "calendar-data") {
            resource->data = mReader->readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
        } else {
            mReader->skipCurrentElement();
        }
    }
}

QList<Buteo::Dav::Resource> Buteo::Dav::Resource::fromData(const QByteArray &data, bool *isOk)
{
    Reader reader;
    reader.read(data);
    if (isOk)
        *isOk = !reader.hasError();
    return reader.results();
}
