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

#include "reader.h"

#include <QDebug>
#include <QUrl>
#include <QList>
#include <QRegExp>
#include <QByteArray>
#include <QXmlStreamReader>

#include <icalformat.h>
#include <vcalformat.h>
#include <exceptions.h>
#include <memorycalendar.h>

#include <LogMacros.h>

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

    QString ensureUidInVEvent(const QString &data) {
        // Ensure UID is in VEVENT section for single-event VCALENDAR blobs.
        int eventCount = 0; // a value of 1 specifies that we should use the fixed data.
        QStringList fixed;
        QString storedUidLine;
        const char separator = '\n';
        QStringList original = data.split(separator);
        bool inVEventSection = false;
        for (QStringList::const_iterator it = original.constBegin(); it != original.constEnd(); it++) {
            const QString &line(*it);
            if (line.startsWith("END:VEVENT")) {
                inVEventSection = false;
            } else if (line.startsWith("BEGIN:VEVENT")) {
                ++eventCount;
                inVEventSection = true;
                fixed.append(line);
                if (!storedUidLine.isEmpty()) {
                    fixed.append(storedUidLine);
                    LOG_DEBUG("The UID was before VEVENT data! Report a bug to the application that generated this file.");
                    continue; // BEGIN:VEVENT line already appended
                }
                eventCount = -1;
                break; // use original iCalData if got to VEVENT without finding UID
            } else if (line.startsWith("UID")) {
                if (!inVEventSection) {
                    storedUidLine = line;
                    continue; // do not append UID line yet
                }
            }
            fixed.append(line);
        }
        // if we found exactly one event and were able to set its UID, return the fixed data.
        // otherwise, return the original data.
        return eventCount == 1 ? fixed.join(separator) : data;
    }

    QString preprocessIcsData(const QString &data) {
        QString temp = data.trimmed();
        temp.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        temp.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
        temp = temp.append(QStringLiteral("\r\n\r\n"));
        temp = ensureUidInVEvent(temp);
        return temp;
    }

    QString ensureICalVersion(const QString &data) {
        // Add VERSION:2.0 after the VCALENDAR tag to force iCal parsing.
        const char separator = '\n';
        QStringList original = data.split(separator);
        QStringList fixed;

        for (QStringList::const_iterator it = original.constBegin(); it != original.constEnd(); it++) {
            const QString &line(*it);

            fixed.append(line);
            if (line.startsWith("BEGIN:VCALENDAR")) {
                fixed.append(QStringLiteral("VERSION:2.0\r"));
            }
        }
        return fixed.join(separator);
    }
}

Reader::Reader(QObject *parent)
    : QObject(parent)
    , mReader(0)
    , mValidResponse(false)
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

const QList<Reader::CalendarResource>& Reader::results() const
{
    return mResults;
}

void Reader::readMultiStatus()
{
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "response") {
            readResponse();
        }
    }
}

void Reader::readResponse()
{
    CalendarResource resource;
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "href") {
            resource.href = QUrl::fromPercentEncoding(mReader->readElementText().toLatin1());
        } else if (mReader->name() == "propstat") {
            readPropStat(resource);
        }
    }
    if (resource.href.isEmpty()) {
        LOG_WARNING("Ignoring received calendar object data, is missing href value");
        return;
    }
    if (!resource.iCalData.trimmed().isEmpty()) {
        bool parsed = true;
        QString icsData = preprocessIcsData(resource.iCalData);
        KCalCore::ICalFormat iCalFormat;
        KCalCore::MemoryCalendar::Ptr cal(new KCalCore::MemoryCalendar(KDateTime::UTC));
        if (!iCalFormat.fromString(cal, icsData)) {
            if (iCalFormat.exception() && iCalFormat.exception()->code()
                == KCalCore::Exception::CalVersion1) {
                KCalCore::VCalFormat vCalFormat;
                if (!vCalFormat.fromString(cal, icsData)) {
                    LOG_WARNING("unable to parse vCal data");
                    parsed = false;
                }
            } else if (iCalFormat.exception()
                       && (iCalFormat.exception()->code()
                           == KCalCore::Exception::CalVersionUnknown
                           || iCalFormat.exception()->code()
                           == KCalCore::Exception::VersionPropertyMissing)) {
                iCalFormat.setException(0);
                LOG_WARNING("unknown or missing version, trying iCal 2.0");
                icsData = ensureICalVersion(icsData);
                if (!iCalFormat.fromString(cal, icsData)) {
                    LOG_WARNING("unable to parse iCal data, returning" << (iCalFormat.exception() ? iCalFormat.exception()->code() : -1));
                    parsed = false;
                }
            } else {
                LOG_WARNING("unable to parse iCal data, returning" << (iCalFormat.exception() ? iCalFormat.exception()->code() : -1));
                parsed = false;
            }
        }
        if (parsed) {
            const KCalCore::Incidence::List incidences = cal->incidences();
            LOG_DEBUG("iCal data contains" << incidences.count() << " incidences");
            if (incidences.count()) {
                QString uid = incidences.first()->uid();
                // In case of more than one incidence, it contains some
                // recurring event information, with exception / RECURRENCE-ID defined.
                for (const KCalCore::Incidence::Ptr &incidence : incidences) {
                    if (incidence->uid() != uid) {
                        LOG_WARNING("iCal data contains invalid incidences with conflicting uids");
                        uid.clear();
                        break;
                    }
                }
                if (!uid.isEmpty()) {
                    for (const KCalCore::Incidence::Ptr &incidence : incidences) {
                        if (incidence->type() == KCalCore::IncidenceBase::TypeEvent
                            || incidence->type() == KCalCore::IncidenceBase::TypeTodo)
                            resource.incidences.append(incidence);
                    }
                }
                LOG_DEBUG("parsed" << resource.incidences.count() << "events or todos from the iCal data");
            } else {
                LOG_WARNING("iCal data doesn't contain a valid incidence");
            }
        }
    }

    mResults.append(resource);
}

void Reader::readPropStat(CalendarResource &resource)
{
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "prop") {
            readProp(resource);
        } else if (mReader->name() == "status") {
            resource.status = mReader->readElementText();
        }
    }
}

void Reader::readProp(CalendarResource &resource)
{
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "getetag") {
            resource.etag = mReader->readElementText();
        } else if (mReader->name() == "calendar-data") {
            resource.iCalData = mReader->readElementText(QXmlStreamReader::IncludeChildElements);
        }
    }
}
