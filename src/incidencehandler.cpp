/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2014 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Bea Lam <bea.lam@jollamobile.com>
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

#include "incidencehandler.h"

#include <QDebug>

#include "logging.h"

#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/ICalFormat>

#define PROP_DTEND_ADDED_USING_DTSTART "dtend-added-as-dtstart"

IncidenceHandler::IncidenceHandler()
{
}

IncidenceHandler::~IncidenceHandler()
{
}

// A given incidence has been added or modified locally.
// To upsync the change, we need to construct the .ics data to upload to server.
// Since the incidence may be an occurrence or recurring series incidence,
// we cannot simply convert the incidence to iCal data, but instead we have to
// upsync an .ics containing the whole recurring series.
QString IncidenceHandler::toIcs(const KCalendarCore::Incidence::Ptr incidence,
                                const KCalendarCore::Incidence::List instances)
{
    // create an in-memory calendar
    // add to it the required incidences (ie, check if has recurrenceId -> load parent and all instances; etc)
    // for each of those, we need to do the IncidenceToExport() modifications first
    // then, export from that calendar to .ics file.
    KCalendarCore::MemoryCalendar::Ptr memoryCalendar(new KCalendarCore::MemoryCalendar(QTimeZone::utc()));

    KCalendarCore::Incidence::Ptr exportableIncidence = IncidenceHandler::incidenceToExport(incidence, instances);
    // store the base recurring event into the in-memory calendar
    if (!memoryCalendar->addIncidence(exportableIncidence)) {
        qCWarning(lcCalDav) << "Unable to add base series event to in-memory calendar for incidence:"
                    << incidence->uid() << ":" << incidence->recurrenceId().toString();
        return QString();
    }
    // now create the persistent occurrences in the in-memory calendar
    for (KCalendarCore::Incidence::Ptr instance : instances) {
        KCalendarCore::Incidence::Ptr exportableOccurrence = IncidenceHandler::incidenceToExport(instance);

        if (!memoryCalendar->addIncidence(exportableOccurrence)) {
            qCWarning(lcCalDav) << "Unable to add this incidence to in-memory calendar for export:"
                        << instance->uid() << instance->recurrenceId().toString();
            return QString();
        }
    }

    KCalendarCore::ICalFormat icalFormat;
    return icalFormat.toString(memoryCalendar, QString(), false);
}

KCalendarCore::Incidence::Ptr IncidenceHandler::incidenceToExport(KCalendarCore::Incidence::Ptr sourceIncidence,
                                                                  const KCalendarCore::Incidence::List &instances)
{
    KCalendarCore::Incidence::Ptr incidence = QSharedPointer<KCalendarCore::Incidence>(sourceIncidence->clone());

    // check to see if the UID is of the special form: NBUID:NotebookUid:EventUid.  If so, trim it.
    if (incidence->uid().startsWith(QStringLiteral("NBUID:"))) {
        QString oldUid = incidence->uid();
        QString trimmedUid = oldUid.mid(oldUid.indexOf(':', 6)+1); // remove NBUID:NotebookUid:
        incidence->setUid(trimmedUid); // leaving just the EventUid.
    }

    // remove any (obsolete) markers that tell us that the time was added by us
    incidence->removeCustomProperty("buteo", "dtstart-date_only");
    incidence->removeCustomProperty("buteo", "dtend-date_only");

    // remove any URI or ETAG data we insert into the event for sync purposes.
    incidence->removeCustomProperty("buteo", "uri");
    incidence->removeCustomProperty("buteo", "etag");
    const QStringList &comments(incidence->comments());
    for (const QString &comment : comments) {
        if ((comment.startsWith("buteo:caldav:uri:")
             || comment.startsWith("buteo:caldav:detached-and-synced")
             || comment.startsWith("buteo:caldav:etag:"))
            && incidence->removeComment(comment)) {
            qCDebug(lcCalDav) << "Discarding buteo-prefixed comment:" << comment;
        }
    }

    // remove EXDATE values from the recurring incidence which correspond to the persistent occurrences (instances)
    if (incidence->recurs()) {
        for (KCalendarCore::Incidence::Ptr instance : instances) {
            KCalendarCore::DateTimeList exDateTimes = incidence->recurrence()->exDateTimes();
            exDateTimes.removeAll(instance->recurrenceId());
            incidence->recurrence()->setExDateTimes(exDateTimes);
            qCDebug(lcCalDav) << "Discarding exdate:" << instance->recurrenceId().toString();
        }
    }

    switch (incidence->type()) {
    case KCalendarCore::IncidenceBase::TypeEvent: {
        KCalendarCore::Event::Ptr event = incidence.staticCast<KCalendarCore::Event>();
        bool eventIsAllDay = event->allDay();
        if (eventIsAllDay) {
            bool sendWithoutDtEnd = !event->customProperty("buteo", PROP_DTEND_ADDED_USING_DTSTART).isEmpty()
                && (event->dtStart() == event->dtEnd());
            event->removeCustomProperty("buteo", PROP_DTEND_ADDED_USING_DTSTART);

            if (sendWithoutDtEnd) {
                // A single-day all-day event was received without a DTEND, and it is still a single-day
                // all-day event, so remove the DTEND before upsyncing.
                qCDebug(lcCalDav) << "Removing DTEND from" << incidence->uid();
                event->setDtEnd(QDateTime());
            }
        }

        // setting dtStart/End changes the allDay value, so ensure it is still set to true if needed.
        if (eventIsAllDay) {
            event->setAllDay(true);
        }
        break;
    }
    case KCalendarCore::IncidenceBase::TypeTodo:
        break;
    default:
        qCDebug(lcCalDav) << "Incidence type not supported; cannot create proper exportable version";
        break;
    }

    return incidence;
}

QString IncidenceHandler::etag(KCalendarCore::Incidence::Ptr incidence)
{
    const QStringList &comments(incidence->comments());
    for (const QString &comment : comments) {
        if (comment.startsWith("buteo:caldav:etag:")) {
            return comment.mid(18);
        }
    }
    return QString();
}

// mKCal deleted custom properties of deleted incidences.
// This was problematic for sync, as we need some fields
// (resource URI and ETAG) in order to sync properly.
// Hence, we abuse the COMMENTS field of the incidence.
QString IncidenceHandler::uri(KCalendarCore::Incidence::Ptr incidence)
{
    const QStringList &comments(incidence->comments());
    for (const QString &comment : comments) {
        if (comment.startsWith("buteo:caldav:uri:")) {
            QString uri = comment.mid(17);
            if (uri.contains('%')) {
                // if it contained a % or a space character, we percent-encoded
                // the uri before storing it, because otherwise kcal doesn't
                // split the comments properly.
                uri = QUrl::fromPercentEncoding(uri.toUtf8());
                qCDebug(lcCalDav) << "URI comment was percent encoded:" << comment << ", returning uri:" << uri;
            }
            if (uri.isEmpty()) {
                qCWarning(lcCalDav) << "Stored uri was empty for:" << incidence->uid()
                                    << incidence->recurrenceId().toString();
            }
            return uri;
        }
    }
    // qCWarning(lcCalDav) << "Returning empty uri for:" << incidence->uid() << incidence->recurrenceId().toString();
    return QString();
}

void IncidenceHandler::setUri(KCalendarCore::Incidence::Ptr incidence,
                              const QString &uri)
{
    const QStringList &comments(incidence->comments());
    for (const QString &comment : comments) {
        if (comment.startsWith("buteo:caldav:uri:")
            && incidence->removeComment(comment)) {
            break;
        }
    }
    if (uri.contains('%') || uri.contains(' ')) {
        // need to percent-encode the uri before storing it,
        // otherwise mkcal doesn't split the comments correctly.
        incidence->addComment(QStringLiteral("buteo:caldav:uri:%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(uri))));
    } else {
        incidence->addComment(QStringLiteral("buteo:caldav:uri:%1").arg(uri));
    }
}
void IncidenceHandler::setEtag(KCalendarCore::Incidence::Ptr incidence,
                               const QString &etag)
{
    const QStringList &comments(incidence->comments());
    for (const QString &comment : comments) {
        if (comment.startsWith("buteo:caldav:etag:")
            && incidence->removeComment(comment)) {
            break;
        }
    }
    incidence->addComment(QStringLiteral("buteo:caldav:etag:%1").arg(etag));
}

void IncidenceHandler::setSyncedException(KCalendarCore::Incidence::Ptr incidence)
{
    // Add a flag to distinguish persistent exceptions that have
    // been detached during the sync process (with the flag)
    // or by a call to dissociateSingleOccurrence() outside
    // of the sync process (in that later case, the incidence
    // will have to be treated as a local addition of a persistent
    // exception, see the NotebookSyncAgent::calculateDelta() function).
    incidence->removeComment("buteo:caldav:detached-and-synced");
    incidence->addComment("buteo:caldav:detached-and-synced");
}

bool IncidenceHandler::isLocalException(KCalendarCore::Incidence::Ptr incidence)
{
    if (incidence->recurrenceId().isNull())
        return false;

    const QStringList &comments(incidence->comments());
    for (const QString &comment : comments) {
        if (comment == "buteo:caldav:detached-and-synced") {
            return false;
        }
    }
    return true;
}

const QByteArray APP = QByteArrayLiteral("VOLATILE");
const QByteArray FAILURE = QByteArrayLiteral("SYNC-FAILURE");
const QByteArray RESOLUTION = QByteArrayLiteral("SYNC-FAILURE-RESOLUTION");

void IncidenceHandler::flagSuccess(const KCalendarCore::Incidence::Ptr &incidence)
{
    incidence->removeCustomProperty(APP, FAILURE);
    incidence->removeCustomProperty(APP, RESOLUTION);
}

void IncidenceHandler::flagUpdateFailure(const KCalendarCore::Incidence::Ptr &incidence)
{
    incidence->setCustomProperty(APP, FAILURE, QStringLiteral("update"));
}

bool IncidenceHandler::updateIsAllowed(const KCalendarCore::Incidence::Ptr &incidence)
{
    return incidence->customProperty(APP, FAILURE).isEmpty()
        || (incidence->customProperty(APP, FAILURE).startsWith(QStringLiteral("update"))
            && incidence->customProperty(APP, RESOLUTION).isEmpty());
}

bool IncidenceHandler::resetUpdateFailure(const KCalendarCore::Incidence::Ptr &incidence)
{
    return incidence->customProperty(APP, FAILURE) == QStringLiteral("update")
        && incidence->customProperty(APP, RESOLUTION) == QStringLiteral("device-reset");
}

void IncidenceHandler::flagDeleteFailure(const KCalendarCore::Incidence::Ptr &incidence)
{
    incidence->setCustomProperty(APP, FAILURE, QStringLiteral("delete"));
}

bool IncidenceHandler::deleteIsAllowed(const KCalendarCore::Incidence::Ptr &incidence)
{
    return incidence->customProperty(APP, FAILURE).isEmpty()
        || (incidence->customProperty(APP, FAILURE).startsWith(QStringLiteral("delete"))
            && incidence->customProperty(APP, RESOLUTION).isEmpty());
}

bool IncidenceHandler::resetDeleteFailure(const KCalendarCore::Incidence::Ptr &incidence)
{
    return incidence->customProperty(APP, FAILURE) == QStringLiteral("delete")
        && incidence->customProperty(APP, RESOLUTION) == QStringLiteral("device-reset");
}

void IncidenceHandler::flagUploadFailure(const KCalendarCore::Incidence::Ptr &incidence)
{
    if (etag(incidence).isEmpty())
        incidence->setCustomProperty(APP, FAILURE, QStringLiteral("upload-new"));
    else
        incidence->setCustomProperty(APP, FAILURE, QStringLiteral("upload"));
}

bool IncidenceHandler::uploadIsAllowed(const KCalendarCore::Incidence::Ptr &incidence)
{
    if (etag(incidence).isEmpty()) {
        return incidence->customProperty(APP, FAILURE).isEmpty()
            || (incidence->customProperty(APP, FAILURE).startsWith(QStringLiteral("upload"))
                && incidence->customProperty(APP, RESOLUTION).isEmpty());
    } else {
        return incidence->customProperty(APP, FAILURE).startsWith(QStringLiteral("upload"))
            && incidence->customProperty(APP, RESOLUTION).isEmpty();
    }
}

bool IncidenceHandler::resetUploadFailure(const KCalendarCore::Incidence::Ptr &incidence)
{
    return incidence->customProperty(APP, FAILURE) == QStringLiteral("upload")
        && incidence->customProperty(APP, RESOLUTION) == QStringLiteral("server-reset");
}
