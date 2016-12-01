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
#include <QXmlStreamReader>

#include <icalformat.h>
#include <vcalformat.h>
#include <memorycalendar.h>

#include <LogMacros.h>

namespace {
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
    mReader = new QXmlStreamReader(data);

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

const QMultiHash<QString, Reader::CalendarResource>& Reader::results() const
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
            readPropStat(&resource);
        }
    }
    if (resource.href.isEmpty()) {
        LOG_WARNING("Ignoring received calendar object data, is missing href value");
        return;
    }
    if (!resource.iCalData.trimmed().isEmpty()) {
        bool parsed = true;
        bool isVCalFormat = false;
        QString icsData = preprocessIcsData(resource.iCalData);
        KCalCore::ICalFormat iCalFormat;
        KCalCore::MemoryCalendar::Ptr cal(new KCalCore::MemoryCalendar(KDateTime::UTC));
        if (!iCalFormat.fromString(cal, icsData)) {
            LOG_WARNING("unable to parse iCal data, trying vCal");
            KCalCore::VCalFormat vCalFormat;
            if (vCalFormat.fromString(cal, icsData)) {
                isVCalFormat = true;
            } else {
                LOG_WARNING("unable to parse vCal data");
                parsed = false;
            }
        }
        if (parsed) {
            KCalCore::Event::List events = cal->events(); // TODO: incidences() not just events()
            LOG_DEBUG("iCal data contains" << cal->events().count() << "VEVENT instances");
            if (events.count() <= 1) {
                // single event, or journal/todos
                if (isVCalFormat && events.count() == 1) {
                    resource.incidences = KCalCore::Incidence::List() << events.first();
                } else {
                    KCalCore::Incidence::Ptr incidence = iCalFormat.fromString(icsData);
                    if (!incidence.isNull()) {
                        resource.incidences = KCalCore::Incidence::List() << incidence;
                    } else {
                        LOG_WARNING("iCal data doesn't contain a valid incidence");
                    }
                }
            } else {
                // contains some recurring event information, with exception / RECURRENCE-ID defined.
                QString eventUid = events.first()->uid();
                Q_FOREACH (const KCalCore::Event::Ptr &event, events) {
                    if (event->uid() != eventUid) {
                        LOG_WARNING("iCal data contains invalid events with conflicting uids");
                        eventUid.clear();
                        break;
                    }
                }
                if (!eventUid.isEmpty()) {
                    Q_FOREACH (const KCalCore::Event::Ptr &event, events) {
                        resource.incidences.append(event);
                    }
                }
                LOG_DEBUG("parsed" << resource.incidences.count() << "events from the iCal data");
            }
        }
    }

    mResults.insert(resource.href, resource); // multihash insert.
}

void Reader::readPropStat(CalendarResource *resource)
{
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "prop") {
            readProp(resource);
        } else if (mReader->name() == "status") {
            resource->status = mReader->readElementText();
        }
    }
}

void Reader::readProp(CalendarResource *resource)
{
    while (mReader->readNextStartElement()) {
        if (mReader->name() == "getetag") {
            resource->etag = mReader->readElementText();
        } else if (mReader->name() == "calendar-data") {
            resource->iCalData = mReader->readElementText();
        }
    }
}
