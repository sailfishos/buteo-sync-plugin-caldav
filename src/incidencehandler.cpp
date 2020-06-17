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
#include <QBuffer>
#include <QDataStream>

#include <LogMacros.h>

#include <memorycalendar.h>
#include <icalformat.h>

#define PROP_DTEND_ADDED_USING_DTSTART "dtend-added-as-dtstart"

#define COPY_IF_NOT_EQUAL(dest, src, get, set) \
{ \
    if (dest->get != src->get) { \
        dest->set(src->get); \
    } \
}

#define RETURN_FALSE_IF_NOT_EQUAL(a, b, func, desc) {\
    if (a->func != b->func) {\
        LOG_DEBUG("Incidence" << desc << "" << "properties are not equal:" << a->func << b->func); \
        return false;\
    }\
}

#define RETURN_FALSE_IF_NOT_TRIMMED_EQUAL(a, b, func, desc) {\
        if (a->func.trimmed() != b->func.trimmed()) {\
            LOG_DEBUG("Incidence" << desc << "" << "properties are not equal:" << a->func.trimmed() << b->func.trimmed()); \
        return false;\
    }\
}

#define RETURN_FALSE_IF_NOT_EQUAL_CUSTOM(failureCheck, desc, debug) {\
    if (failureCheck) {\
        LOG_DEBUG("Incidence" << desc << "properties are not equal:" << desc << debug); \
        return false;\
    }\
}

IncidenceHandler::IncidenceHandler()
{
}

IncidenceHandler::~IncidenceHandler()
{
}

void IncidenceHandler::normalizePersonEmail(KCalCore::Person *p)
{
    QString email = p->email().replace(QStringLiteral("mailto:"), QString(), Qt::CaseInsensitive);
    if (email != p->email()) {
        p->setEmail(email);
    }
}

template <typename T>
bool IncidenceHandler::pointerDataEqual(const QVector<QSharedPointer<T> > &vectorA, const QVector<QSharedPointer<T> > &vectorB)
{
    if (vectorA.count() != vectorB.count()) {
        return false;
    }
    for (int i=0; i<vectorA.count(); i++) {
        if (*vectorA[i].data() != *vectorB[i].data()) {
            return false;
        }
    }
    return true;
}

void IncidenceHandler::copyIncidenceProperties(KCalCore::Incidence::Ptr dest, const KCalCore::Incidence::Ptr &src)
{
    if (!dest || !src) {
        qWarning() << "Invalid parameters!";
        return;
    }
    if (dest->type() != src->type()) {
        qWarning() << "incidences do not have same type!";
        return;
    }

    KDateTime origCreated = dest->created();
    KDateTime origLastModified = dest->lastModified();

    // Copy recurrence information if required.
    if (*(dest->recurrence()) != *(src->recurrence())) {
        dest->recurrence()->clear();

        KCalCore::Recurrence *dr = dest->recurrence();
        KCalCore::Recurrence *sr = src->recurrence();

        // recurrence rules and dates
        KCalCore::RecurrenceRule::List srRRules = sr->rRules();
        for (QList<KCalCore::RecurrenceRule*>::const_iterator it = srRRules.constBegin(), end = srRRules.constEnd(); it != end; ++it) {
            KCalCore::RecurrenceRule *r = new KCalCore::RecurrenceRule(*(*it));
            dr->addRRule(r);
        }
        dr->setRDates(sr->rDates());
        dr->setRDateTimes(sr->rDateTimes());

        // exception rules and dates
        KCalCore::RecurrenceRule::List srExRules = sr->exRules();
        for (QList<KCalCore::RecurrenceRule*>::const_iterator it = srExRules.constBegin(), end = srExRules.constEnd(); it != end; ++it) {
            KCalCore::RecurrenceRule *r = new KCalCore::RecurrenceRule(*(*it));
            dr->addExRule(r);
        }
        dr->setExDates(sr->exDates());
        dr->setExDateTimes(sr->exDateTimes());
    }

    // copy the duration before the dtEnd as calling setDuration() changes the dtEnd
    COPY_IF_NOT_EQUAL(dest, src, duration(), setDuration);

    if (dest->type() == KCalCore::IncidenceBase::TypeEvent && src->type() == KCalCore::IncidenceBase::TypeEvent) {
        KCalCore::Event::Ptr destEvent = dest.staticCast<KCalCore::Event>();
        KCalCore::Event::Ptr srcEvent = src.staticCast<KCalCore::Event>();
        COPY_IF_NOT_EQUAL(destEvent, srcEvent, dtEnd(), setDtEnd);
        COPY_IF_NOT_EQUAL(destEvent, srcEvent, transparency(), setTransparency);
    }

    if (dest->type() == KCalCore::IncidenceBase::TypeTodo && src->type() == KCalCore::IncidenceBase::TypeTodo) {
        KCalCore::Todo::Ptr destTodo = dest.staticCast<KCalCore::Todo>();
        KCalCore::Todo::Ptr srcTodo = src.staticCast<KCalCore::Todo>();
        COPY_IF_NOT_EQUAL(destTodo, srcTodo, completed(), setCompleted);
        COPY_IF_NOT_EQUAL(destTodo, srcTodo, dtDue(), setDtDue);
        COPY_IF_NOT_EQUAL(destTodo, srcTodo, dtRecurrence(), setDtRecurrence);
        COPY_IF_NOT_EQUAL(destTodo, srcTodo, percentComplete(), setPercentComplete);
    }

    // dtStart and dtEnd changes allDay value, so must set those before copying allDay value
    COPY_IF_NOT_EQUAL(dest, src, dtStart(), setDtStart);
    COPY_IF_NOT_EQUAL(dest, src, allDay(), setAllDay);

    COPY_IF_NOT_EQUAL(dest, src, hasDuration(), setHasDuration);
    COPY_IF_NOT_EQUAL(dest, src, organizer(), setOrganizer);
    COPY_IF_NOT_EQUAL(dest, src, isReadOnly(), setReadOnly);

    if (!pointerDataEqual(src->attendees(), dest->attendees())) {
        dest->clearAttendees();
        const KCalCore::Attendee::List attendees = src->attendees();
        for (const KCalCore::Attendee::Ptr &attendee : attendees) {
            dest->addAttendee(attendee);
        }
    }

    if (src->comments() != dest->comments()) {
        dest->clearComments();
        const QStringList comments = src->comments();
        for (const QString &comment : comments) {
            dest->addComment(comment);
        }
    }
    if (src->contacts() != dest->contacts()) {
        dest->clearContacts();
        const QStringList contacts = src->contacts();
        for (const QString &contact : contacts) {
            dest->addContact(contact);
        }
    }

    COPY_IF_NOT_EQUAL(dest, src, altDescription(), setAltDescription);
    COPY_IF_NOT_EQUAL(dest, src, categories(), setCategories);
    COPY_IF_NOT_EQUAL(dest, src, customStatus(), setCustomStatus);
    COPY_IF_NOT_EQUAL(dest, src, description(), setDescription);
    COPY_IF_NOT_EQUAL(dest, src, geoLatitude(), setGeoLatitude);
    COPY_IF_NOT_EQUAL(dest, src, geoLongitude(), setGeoLongitude);
    COPY_IF_NOT_EQUAL(dest, src, hasGeo(), setHasGeo);
    COPY_IF_NOT_EQUAL(dest, src, location(), setLocation);
    COPY_IF_NOT_EQUAL(dest, src, resources(), setResources);
    COPY_IF_NOT_EQUAL(dest, src, secrecy(), setSecrecy);
    COPY_IF_NOT_EQUAL(dest, src, status(), setStatus);
    COPY_IF_NOT_EQUAL(dest, src, summary(), setSummary);
    COPY_IF_NOT_EQUAL(dest, src, revision(), setRevision);

    if (!pointerDataEqual(src->alarms(), dest->alarms())) {
        dest->clearAlarms();
        const KCalCore::Alarm::List alarms = src->alarms();
        for (const KCalCore::Alarm::Ptr &alarm : alarms) {
            dest->addAlarm(alarm);
        }
    }

    if (!pointerDataEqual(src->attachments(), dest->attachments())) {
        dest->clearAttachments();
        const KCalCore::Attachment::List attachments = src->attachments();
        for (const KCalCore::Attachment::Ptr &attachment : attachments) {
            dest->addAttachment(attachment);
        }
    }

    // Ensure all custom properties are copied also.
    QSharedPointer<KCalCore::CustomProperties> srcCP =
        src.staticCast<KCalCore::CustomProperties>();
    QSharedPointer<KCalCore::CustomProperties> destCP =
        dest.staticCast<KCalCore::CustomProperties>();
    if (!(*srcCP.data() == *destCP.data()))
        *destCP.data() = *srcCP.data();

    // Don't change created and lastModified properties as that affects mkcal
    // calculations for when the incidence was added and modified in the db.
    if (origCreated != dest->created()) {
        dest->setCreated(origCreated);
    }
    if (origLastModified != dest->lastModified()) {
        dest->setLastModified(origLastModified);
    }
}

void IncidenceHandler::prepareImportedIncidence(KCalCore::Incidence::Ptr incidence)
{
    if (incidence->type() != KCalCore::IncidenceBase::TypeEvent) {
        LOG_WARNING("unable to handle imported non-event incidence; skipping");
        return;
    }

    switch (incidence->type()) {
    case KCalCore::IncidenceBase::TypeEvent: {
        KCalCore::Event::Ptr event = incidence.staticCast<KCalCore::Event>();

        if (event->allDay()) {
            KDateTime dtStart = event->dtStart();
            KDateTime dtEnd = event->dtEnd();

            // calendar processing requires all-day events to have a dtEnd
            if (!dtEnd.isValid()) {
                LOG_DEBUG("Adding DTEND to" << incidence->uid() << "as" << dtStart.toString());
                event->setCustomProperty("buteo", PROP_DTEND_ADDED_USING_DTSTART, PROP_DTEND_ADDED_USING_DTSTART);
                event->setDtEnd(dtStart);
            }

            // setting dtStart/End changes the allDay value, so ensure it is still set to true
            event->setAllDay(true);
        }
        break;
    }
    default:
        break;
    }
}

// A given incidence has been added or modified locally.
// To upsync the change, we need to construct the .ics data to upload to server.
// Since the incidence may be an occurrence or recurring series incidence,
// we cannot simply convert the incidence to iCal data, but instead we have to
// upsync an .ics containing the whole recurring series.
QString IncidenceHandler::toIcs(const KCalCore::Incidence::Ptr incidence,
                                const KCalCore::Incidence::List instances)
{
    KCalCore::Incidence::Ptr exportableIncidence = IncidenceHandler::incidenceToExport(incidence, instances);

    // create an in-memory calendar
    // add to it the required incidences (ie, check if has recurrenceId -> load parent and all instances; etc)
    // for each of those, we need to do the IncidenceToExport() modifications first
    // then, export from that calendar to .ics file.
    KCalCore::MemoryCalendar::Ptr memoryCalendar(new KCalCore::MemoryCalendar(KDateTime::UTC));
    // store the base recurring event into the in-memory calendar
    if (!memoryCalendar->addIncidence(exportableIncidence)) {
        LOG_WARNING("Unable to add base series event to in-memory calendar for incidence:"
                    << incidence->uid() << ":" << incidence->recurrenceId().toString());
        return QString();
    }
    // now create the persistent occurrences in the in-memory calendar
    for (KCalCore::Incidence::Ptr instance : instances) {
        // We cannot call dissociateSingleOccurrence() on the MemoryCalendar
        // as that's an mKCal specific function.
        // We cannot call dissociateOccurrence() because that function
        // takes only a QDate instead of a KDateTime recurrenceId.
        // Thus, we need to manually create an exception occurrence.
        KCalCore::Incidence::Ptr exportableOccurrence(exportableIncidence->clone());
        exportableOccurrence->setCreated(instance->created());
        exportableOccurrence->setRevision(instance->revision());
        exportableOccurrence->clearRecurrence();
        exportableOccurrence->setRecurrenceId(instance->recurrenceId());
        exportableOccurrence->setDtStart(instance->recurrenceId());

        // add it, and then update it in-memory.
        if (!memoryCalendar->addIncidence(exportableOccurrence)) {
            LOG_WARNING("Unable to add this incidence to in-memory calendar for export:"
                        << instance->uid() << instance->recurrenceId().toString());
            return QString();
        } else {
            KCalCore::Incidence::Ptr reloadedOccurrence = memoryCalendar->incidence(exportableIncidence->uid(), instance->recurrenceId());
            if (!reloadedOccurrence) {
                LOG_WARNING("Unable to find this incidence within in-memory calendar for export:"
                            << exportableIncidence->uid() << instance->recurrenceId().toString());
                return QString();
            }
            reloadedOccurrence->startUpdates();
            IncidenceHandler::copyIncidenceProperties(reloadedOccurrence, IncidenceHandler::incidenceToExport(instance));
            reloadedOccurrence->endUpdates();
        }
    }

    KCalCore::ICalFormat icalFormat;
    return icalFormat.toString(memoryCalendar, QString(), false);
}

KCalCore::Incidence::Ptr IncidenceHandler::incidenceToExport(KCalCore::Incidence::Ptr sourceIncidence, const KCalCore::Incidence::List &instances)
{
    KCalCore::Incidence::Ptr incidence = QSharedPointer<KCalCore::Incidence>(sourceIncidence->clone());

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
        if (comment.startsWith("buteo:caldav:uri:") ||
            comment.startsWith("buteo:caldav:detached-and-synced") ||
            comment.startsWith("buteo:caldav:etag:")) {
            LOG_DEBUG("Discarding buteo-prefixed comment:" << comment);
            incidence->removeComment(comment);
        }
    }

    // The default storage implementation applies the organizer as an attendee by default.
    // Undo this as it turns the incidence into a scheduled event requiring acceptance/rejection/etc.
    const KCalCore::Person::Ptr organizer = incidence->organizer();
    if (organizer) {
        const KCalCore::Attendee::List attendees = incidence->attendees();
        for (const KCalCore::Attendee::Ptr &attendee : attendees) {
            if (attendee->email() == organizer->email() && attendee->fullName() == organizer->fullName()) {
                LOG_DEBUG("Discarding organizer as attendee" << attendee->fullName());
                incidence->deleteAttendee(attendee);
            } else {
                LOG_DEBUG("Not discarding attendee:" << attendee->fullName() << attendee->email() << ": not organizer:" << organizer->fullName() << organizer->email());
            }
        }
    }

    // remove EXDATE values from the recurring incidence which correspond to the persistent occurrences (instances)
    if (incidence->recurs()) {
        for (KCalCore::Incidence::Ptr instance : instances) {
            KCalCore::DateTimeList exDateTimes = incidence->recurrence()->exDateTimes();
            exDateTimes.removeAll(instance->recurrenceId());
            incidence->recurrence()->setExDateTimes(exDateTimes);
            LOG_DEBUG("Discarding exdate:" << instance->recurrenceId().toString());
        }
    }

    // Icalformat from kcalcore converts second-type duration into day-type duration
    // whenever possible. We do the same to have consistent comparisons.
    const KCalCore::Alarm::List alarms = incidence->alarms();
    for (KCalCore::Alarm::Ptr alarm : alarms) {
        if (!alarm->hasTime()) {
            KCalCore::Duration offset(0);
            if (alarm->hasEndOffset())
                offset = alarm->endOffset();
            else
                offset = alarm->startOffset();
            if (!offset.isDaily() && !(offset.value() % (60 * 60 * 24))) {
                LOG_DEBUG("Converting to day-type duration " << offset.asDays());
                if (alarm->hasEndOffset())
                    alarm->setEndOffset(KCalCore::Duration(offset.asDays(), KCalCore::Duration::Days));
                else
                    alarm->setStartOffset(KCalCore::Duration(offset.asDays(), KCalCore::Duration::Days));
            }
        }
    }

    switch (incidence->type()) {
    case KCalCore::IncidenceBase::TypeEvent: {
        KCalCore::Event::Ptr event = incidence.staticCast<KCalCore::Event>();
        bool eventIsAllDay = event->allDay();
        if (eventIsAllDay) {
            bool sendWithoutDtEnd = !event->customProperty("buteo", PROP_DTEND_ADDED_USING_DTSTART).isEmpty()
                && (event->dtStart() == event->dtEnd());
            event->removeCustomProperty("buteo", PROP_DTEND_ADDED_USING_DTSTART);

            if (sendWithoutDtEnd) {
                // A single-day all-day event was received without a DTEND, and it is still a single-day
                // all-day event, so remove the DTEND before upsyncing.
                LOG_DEBUG("Removing DTEND from" << incidence->uid());
                event->setDtEnd(KDateTime());
            }
        }

        if (event->dtStart().isDateOnly()) {
            KDateTime dt = KDateTime(event->dtStart().date(), KDateTime::Spec::ClockTime());
            dt.setDateOnly(true);
            event->setDtStart(dt);
            LOG_DEBUG("Stripping time from date-only DTSTART:" << dt.toString());
        }

        // setting dtStart/End changes the allDay value, so ensure it is still set to true if needed.
        if (eventIsAllDay) {
            event->setAllDay(true);
        }
        break;
    }
    case KCalCore::IncidenceBase::TypeTodo:
        break;
    default:
        LOG_DEBUG("Incidence type not supported; cannot create proper exportable version");
        break;
    }

    return incidence;
}
