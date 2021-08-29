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
    KCalendarCore::Incidence::Ptr exportableIncidence = IncidenceHandler::incidenceToExport(incidence, instances);

    // create an in-memory calendar
    // add to it the required incidences (ie, check if has recurrenceId -> load parent and all instances; etc)
    // for each of those, we need to do the IncidenceToExport() modifications first
    // then, export from that calendar to .ics file.
    KCalendarCore::MemoryCalendar::Ptr memoryCalendar(new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    // store the base recurring event into the in-memory calendar
    if (!memoryCalendar->addIncidence(exportableIncidence)) {
        qCWarning(lcCalDav) << "Unable to add base series event to in-memory calendar for incidence:"
                    << incidence->uid() << ":" << incidence->recurrenceId().toString();
        return QString();
    }
    // now create the persistent occurrences in the in-memory calendar
    for (KCalendarCore::Incidence::Ptr instance : instances) {
        // We cannot call dissociateSingleOccurrence() on the MemoryCalendar
        // as that's an mKCal specific function.
        // We cannot call dissociateOccurrence() because that function
        // takes only a QDate instead of a KDateTime recurrenceId.
        // Thus, we need to manually create an exception occurrence.
        KCalendarCore::Incidence::Ptr exportableOccurrence(exportableIncidence->clone());
        exportableOccurrence->setCreated(instance->created());
        exportableOccurrence->setRevision(instance->revision());
        exportableOccurrence->clearRecurrence();
        exportableOccurrence->setRecurrenceId(instance->recurrenceId());
        exportableOccurrence->setDtStart(instance->recurrenceId());

        // add it, and then update it in-memory.
        if (!memoryCalendar->addIncidence(exportableOccurrence)) {
            qCWarning(lcCalDav) << "Unable to add this incidence to in-memory calendar for export:"
                        << instance->uid() << instance->recurrenceId().toString();
            return QString();
        } else {
            KCalendarCore::Incidence::Ptr reloadedOccurrence = memoryCalendar->incidence(exportableIncidence->uid(), instance->recurrenceId());
            if (!reloadedOccurrence) {
                qCWarning(lcCalDav) << "Unable to find this incidence within in-memory calendar for export:"
                            << exportableIncidence->uid() << instance->recurrenceId().toString();
                return QString();
            }
            *reloadedOccurrence.staticCast<KCalendarCore::IncidenceBase>() = *IncidenceHandler::incidenceToExport(instance).staticCast<KCalendarCore::IncidenceBase>();
        }
    }

    KCalendarCore::ICalFormat icalFormat;
    return icalFormat.toString(memoryCalendar, QString(), false);
}

KCalendarCore::Incidence::Ptr IncidenceHandler::incidenceToExport(KCalendarCore::Incidence::Ptr sourceIncidence, const KCalendarCore::Incidence::List &instances)
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
        if ((comment.startsWith("buteo:caldav:uri:") ||
             comment.startsWith("buteo:caldav:detached-and-synced") ||
             comment.startsWith("buteo:caldav:etag:"))
            && incidence->removeComment(comment)) {
            qCDebug(lcCalDav) << "Discarding buteo-prefixed comment:" << comment;
        }
    }

    // The default storage implementation applies the organizer as an attendee by default.
    // Undo this as it turns the incidence into a scheduled event requiring acceptance/rejection/etc.
    const KCalendarCore::Person &organizer = incidence->organizer();
    if (!organizer.isEmpty()) {
        KCalendarCore::Attendee::List attendees = incidence->attendees();
        KCalendarCore::Attendee::List::Iterator it = attendees.begin();
        while (it != attendees.end()) {
            if (it->email() == organizer.email() && it->fullName() == organizer.fullName()) {
                qCDebug(lcCalDav) << "Discarding organizer as attendee" << it->fullName();
                it = attendees.erase(it);
            } else {
                qCDebug(lcCalDav) << "Not discarding attendee:" << it->fullName() << it->email() << ": not organizer:" << organizer.fullName() << organizer.email();
                it++;
            }
        }
        incidence->setAttendees(attendees);
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
