/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2026 Jolla Ltd.
 * Copyright (C) 2026 Damien Caliste <dcaliste@free.fr>.
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

#include "localstorage.h"
#include "incidencehandler.h"
#include "logging.h"

#include <notebook.h>

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/VCalFormat>
#include <KCalendarCore/Exceptions>

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
                    qCDebug(lcCalDav) << "The UID was before VEVENT data! Report a bug to the application that generated this file.";
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

    QString preprocessIcsData(const QString &data) {
        QString temp = data;
        temp.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        temp.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
        temp = temp.append(QStringLiteral("\r\n\r\n"));
        temp = ensureUidInVEvent(temp);
        return temp;
    }
}

KCalendarCore::Incidence::List LocalStorage::parseICSData(const QString &data)
{
    bool parsed = true;
    QString icsData = preprocessIcsData(data);
    KCalendarCore::ICalFormat iCalFormat;
    KCalendarCore::MemoryCalendar::Ptr cal(new KCalendarCore::MemoryCalendar(QTimeZone::utc()));
    KCalendarCore::Incidence::List results;
    if (!iCalFormat.fromString(cal, icsData)) {
        if (iCalFormat.exception() && iCalFormat.exception()->code()
            == KCalendarCore::Exception::CalVersion1) {
            KCalendarCore::VCalFormat vCalFormat;
            if (!vCalFormat.fromString(cal, icsData)) {
                qCWarning(lcCalDav) << "unable to parse vCal data";
                parsed = false;
            }
        } else if (iCalFormat.exception()
                   && (iCalFormat.exception()->code()
                       == KCalendarCore::Exception::CalVersionUnknown
                       || iCalFormat.exception()->code()
                       == KCalendarCore::Exception::VersionPropertyMissing)) {
            iCalFormat.setException(0);
            qCWarning(lcCalDav) << "unknown or missing version, trying iCal 2.0";
            icsData = ensureICalVersion(icsData);
            if (!iCalFormat.fromString(cal, icsData)) {
                qCWarning(lcCalDav) << "unable to parse iCal data, returning"
                                    << (iCalFormat.exception() ? iCalFormat.exception()->code() : -1);
                parsed = false;
            }
        } else {
            qCWarning(lcCalDav) << "unable to parse iCal data, returning"
                                << (iCalFormat.exception() ? iCalFormat.exception()->code() : -1);
            parsed = false;
        }
    }
    if (parsed) {
        const KCalendarCore::Incidence::List incidences = cal->incidences();
        qCDebug(lcCalDav) << "iCal data contains" << incidences.count() << " incidences";
        if (incidences.count()) {
            QString uid = incidences.first()->uid();
            // In case of more than one incidence, it contains some
            // recurring event information, with exception / RECURRENCE-ID defined.
            for (const KCalendarCore::Incidence::Ptr &incidence : incidences) {
                if (incidence->uid() != uid) {
                    qCWarning(lcCalDav) << "iCal data contains invalid incidences with conflicting uids";
                    uid.clear();
                    break;
                }
            }
            if (!uid.isEmpty()) {
                for (const KCalendarCore::Incidence::Ptr &incidence : incidences) {
                    if (incidence->type() == KCalendarCore::IncidenceBase::TypeEvent
                        || incidence->type() == KCalendarCore::IncidenceBase::TypeTodo)
                        results.append(incidence);
                }
            }
            qCDebug(lcCalDav) << "parsed" << results.count() << "events or todos from the iCal data";
        } else {
            qCWarning(lcCalDav) << "iCal data doesn't contain a valid incidence";
        }
    }
    return results;
}

LocalStorage::LocalStorage()
{
    mKCal::ExtendedCalendar::Ptr calendar(new mKCal::ExtendedCalendar(QTimeZone::utc()));
    calendar->setUpdateLastModifiedOnChange(false);
    mStorage = mKCal::ExtendedCalendar::defaultStorage(calendar);
    mOpened = mStorage && mStorage->open();
}

LocalStorage::~LocalStorage()
{
}

bool LocalStorage::isOpened() const
{
    return mOpened;
}

QSharedPointer<LocalStorage> LocalStorage::instance()
{
    static QWeakPointer<LocalStorage> storagePtr;
    QSharedPointer<LocalStorage> strong = storagePtr.toStrongRef();
    if (!strong) {
        strong = QSharedPointer<LocalStorage>(new LocalStorage);
        storagePtr = QWeakPointer<LocalStorage>(strong);
    }
    return strong;
}

void LocalStorage::deleteNotebooksForAccount(int accountId)
{
    if (mOpened) {
        const QString accountStr = QString::number(accountId);
        const QString accountPrefix = accountStr + "-"; // for historical reasons!
        const mKCal::Notebook::List notebookList = mStorage->notebooks();
        qCDebug(lcCalDav) << "Total Number of Notebooks in device = " << notebookList.count();
        int deletedCount = 0;
        for (mKCal::Notebook::Ptr notebook : notebookList) {
            if (notebook->account() == accountStr
                || notebook->account().startsWith(accountPrefix)) {
                if (mStorage->deleteNotebook(notebook)) {
                    deletedCount++;
                }
            }
        }
        qCDebug(lcCalDav) << "Deleted" << deletedCount << "notebooks";
    }
}

QSet<int> LocalStorage::notebookAccountIds(const QString &service)
{
    QSet<int> ids;
    if (mOpened) {
        for (mKCal::Notebook::Ptr nb : mStorage->notebooks()) {
            QString nbAccount = nb->account();
            if (!nbAccount.isEmpty() && nb->pluginName().contains(service)) {
                // caldav notebook->account() values used to be like: "55-/user/calendars/someCalendar"
                int indexOfHyphen = nbAccount.indexOf('-');
                if (indexOfHyphen > 0) {
                    // this is an old caldav notebook which used "accountId-remoteServerPath" form
                    nbAccount.chop(nbAccount.length() - indexOfHyphen);
                }
                bool ok = true;
                int notebookAccountId = nbAccount.toInt(&ok);
                if (!ok) {
                    qCWarning(lcCalDav) << "notebook account value was strange:" << nb->account()
                                        << "->" << nbAccount << "->" << "not ok";
                } else {
                    qCWarning(lcCalDav) << "found account id:" << notebookAccountId << "for" << nb->account()
                                        << "->" << nbAccount;
                    ids.insert(notebookAccountId);
                }
            }
        }
    }
    return ids;
}

static const QByteArray PATH_PROPERTY = QByteArrayLiteral("remoteCalendarPath");
static const QByteArray EMAIL_PROPERTY = QByteArrayLiteral("userPrincipalEmail");
static const QByteArray SERVER_COLOR_PROPERTY = QByteArrayLiteral("serverColor");

QString LocalStorage::notebookUidForRemotePath(int accountId,
                                               const QString &remotePath)
{
    // Look for an already existing notebook in storage for this account and path.
    const QString accountStr = QString::number(accountId);
    for (mKCal::Notebook::Ptr notebook : mStorage->notebooks()) {
        if (notebook->account() == accountStr
            && (notebook->customProperty(PATH_PROPERTY) == remotePath
                || notebook->syncProfile().endsWith(QStringLiteral(":%1").arg(remotePath)))) {
            qCDebug(lcCalDav) << "found notebook:" << notebook->uid()
                              << "for remote calendar:" << remotePath;
            mNotebooks.insert(notebook->uid(), notebook);
            return notebook->uid();
        }
    }
    qCDebug(lcCalDav) << "no notebook exists for" << remotePath;
    mKCal::Notebook::Ptr notebook(new mKCal::Notebook);
    notebook->setAccount(accountStr);
    notebook->setCustomProperty(PATH_PROPERTY, remotePath);
    notebook->setCustomProperty(SERVER_COLOR_PROPERTY, QStringLiteral("undefined"));
    mNotebooks.insert(notebook->uid(), notebook);
    return notebook->uid();
}

void LocalStorage::setNotebookMetadata(const QString &notebookUid,
                                       const Buteo::Dav::CalendarInfo &info,
                                       const QString &userEmail,
                                       const QString &pluginName,
                                       const QString &syncProfile)
{
    mKCal::Notebook::Ptr notebook = mNotebooks.value(notebookUid);
    if (!notebook) {
        qCWarning(lcCalDav) << "unknown notebook:" << notebookUid;
        return;
    }

    qCDebug(lcCalDav) << "setting notebook infos:" << notebookUid << info.displayName;
    notebook->setName(info.displayName);
    notebook->setDescription(info.description);
    notebook->setSyncProfile(syncProfile);
    notebook->setPluginName(pluginName);
    notebook->setEventsAllowed(info.allowEvents);
    notebook->setTodosAllowed(info.allowTodos);
    notebook->setJournalsAllowed(info.allowJournals);
    notebook->setCustomProperty(EMAIL_PROPERTY, userEmail);
    if (!info.color.isEmpty()
        && notebook->customProperty(SERVER_COLOR_PROPERTY) != info.color) {
        if (!notebook->customProperty(SERVER_COLOR_PROPERTY).isEmpty()) {
            // Override user-selected notebook color only on each server change
            // and not if there was no server color saved.
            notebook->setColor(info.color);
        }
        notebook->setCustomProperty(SERVER_COLOR_PROPERTY, info.color);
    }
    notebook->setIsReadOnly((info.privileges & Buteo::Dav::READ)
                            && !(info.privileges & Buteo::Dav::WRITE));
}

QDateTime LocalStorage::lastSyncDate(const QString &notebookUid)
{
    mKCal::Notebook::Ptr notebook = mNotebooks.value(notebookUid);
    if (!notebook) {
        qCWarning(lcCalDav) << "unknown notebook:" << notebookUid;
        return QDateTime();
    }

    return notebook->syncDate();
}

bool LocalStorage::deleteNotebook(const QString &notebookUid)
{
    mKCal::Notebook::Ptr notebook = mStorage->notebook(notebookUid);
    // delete the notebook from local database
    return !notebook || mStorage->deleteNotebook(notebook);
}

bool LocalStorage::ensureNotebook(const QString &notebookUid)
{
    mKCal::Notebook::Ptr notebook = mNotebooks.value(notebookUid);
    if (!notebook) {
        qCWarning(lcCalDav) << "unknown notebook:" << notebookUid;
        return false;
    }

    mKCal::Notebook::Ptr localNotebook = mStorage->notebook(notebookUid);
    if (!localNotebook) {
        if (!mStorage->addNotebook(notebook)) {
            qCDebug(lcCalDav) << "Unable to (re)create notebook" << notebook->name()
                              << "for account" << notebook->account();
            return false;
        }
    } else {
        *localNotebook = *notebook;
        if (!mStorage->updateNotebook(localNotebook)) {
            qCWarning(lcCalDav) << "Cannot update notebook" << notebook->name() << "in storage.";
            return false;
        }
    }

    return true;
}

bool LocalStorage::snapshot(const QString &notebookUid, KCalendarCore::Incidence::List *allIncidences,
                            QSet<QString> *modifiedInstances, KCalendarCore::Incidence::List *deletedIncidences)
{
    mKCal::Notebook::Ptr notebook = mNotebooks.value(notebookUid);
    if (!notebook) {
        qCWarning(lcCalDav) << "unknown notebook:" << notebookUid;
        return false;
    }

    if (!allIncidences || !mStorage->allIncidences(allIncidences, notebookUid)) {
        qCWarning(lcCalDav) << "Unable to load incidences of notebook:" << notebookUid;
        return false;
    }
    if (!deletedIncidences || !mStorage->deletedIncidences(deletedIncidences, QDateTime(), notebookUid)) {
        qCWarning(lcCalDav) << "Unable to load deleted incidences of notebook:" << notebookUid;
        return false;
    }
    const QDateTime syncDateTime = notebook->syncDate().addSecs(1);
    for (KCalendarCore::Incidence::Ptr incidence : const_cast<const KCalendarCore::Incidence::List&>(*allIncidences)) {
        if ((incidence->created() < syncDateTime
             && incidence->lastModified() >= syncDateTime)) {
            modifiedInstances->insert(incidence->instanceIdentifier());
        }
    }
    // Store sync time, so any further call to ::snapshot() will get the modified
    // events since the last snapshot.
    notebook->setSyncDate(QDateTime::currentDateTimeUtc());
    return true;
}

Buteo::Dav::Resource LocalStorage::toResource(KCalendarCore::Incidence::Ptr incidence)
{
    Buteo::Dav::Resource res;

    res.href = IncidenceHandler::uri(incidence);
    res.etag = IncidenceHandler::etag(incidence);
    if (incidence->recurs() || incidence->hasRecurrenceId()) {
        if (mStorage->load(incidence->uid())) {
            KCalendarCore::Incidence::Ptr recurringIncidence(incidence->recurs()
                                                             ? incidence
                                                             : mStorage->calendar()->incidence(incidence->uid()));
            if (recurringIncidence) {
                res.etag = IncidenceHandler::etag(recurringIncidence);
                res.data = IncidenceHandler::toIcs(recurringIncidence,
                                                   mStorage->calendar()->instances(recurringIncidence));
            } else {
                qCWarning(lcCalDav) << "Cannot find parent of " << incidence->uid() << "for upload of series.";
                res.status = QStringLiteral("This is an exception occurrence without recurring parent.");
            }
        } else {
            qCWarning(lcCalDav) << "Cannot load series " << incidence->uid();
            res.status = QStringLiteral("Database error, cannot load recurring series for the occurrence.");
        }
    } else {
        res.data = IncidenceHandler::toIcs(incidence);
    }
    if (res.data.isEmpty()) {
        qCDebug(lcCalDav) << "Skipping upload of broken incidence:" << incidence->uid();
        res.status = QStringLiteral("Cannot generate ICS data.");
    }
    return res;
}

QSet<QString> LocalStorage::toDeletedResources(KCalendarCore::Incidence::List deletedIncidences,
                                               KCalendarCore::Incidence::List *modifiedIncidences)
{
    QSet<QString> uris;
    QSet<QString> deletedSeries;
    KCalendarCore::Incidence::List exceptions;
    for (KCalendarCore::Incidence::Ptr incidence : deletedIncidences) {
        if (incidence->hasRecurrenceId()) {
            exceptions.append(incidence);
        } else {
            uris.insert(IncidenceHandler::uri(incidence));
            if (incidence->recurs()) {
                deletedSeries.insert(incidence->uid());
            }
        }
    }
    if (modifiedIncidences) {
        for (KCalendarCore::Incidence::Ptr exception : exceptions) {
            if (!deletedSeries.contains(exception->uid())) {
                modifiedIncidences->append(exception);
            }
        }
    }
    return uris;
}

QString LocalStorage::nbUid(const QString &notebookId, const QString &uid)
{
    return QStringLiteral("NBUID:%1:%2").arg(notebookId).arg(uid);
}

static void updateIncidence(KCalendarCore::Incidence::Ptr incidence,
                            KCalendarCore::Incidence::Ptr storedIncidence)
{
    qCDebug(lcCalDav) << "Updating existing event:" << storedIncidence->uid() << storedIncidence->recurrenceId();

    storedIncidence->startUpdates();
    *storedIncidence.staticCast<KCalendarCore::IncidenceBase>() = *incidence.staticCast<KCalendarCore::IncidenceBase>();
    IncidenceHandler::flagSuccess(storedIncidence);
    storedIncidence->endUpdates();
}

bool LocalStorage::syncFromResource(const QString &notebookUid,
                                    const Buteo::Dav::Resource &resource,
                                    KCalendarCore::Incidence::List *additions,
                                    KCalendarCore::Incidence::List *modifications,
                                    QByteArray *errorMessage)
{
    const KCalendarCore::Incidence::List incidences = parseICSData(resource.data);
    if (incidences.isEmpty())
        return true;

    mKCal::Notebook::Ptr notebook = mNotebooks.value(notebookUid);
    if (!notebook) {
        qCWarning(lcCalDav) << "unknown notebook:" << notebookUid;
        return false;
    }

    // Set-up the default notebook when adding new incidences.
    mStorage->calendar()->addNotebook(notebookUid, true);
    if (!mStorage->calendar()->setDefaultNotebook(notebookUid)) {
        qCWarning(lcCalDav) << "Cannot set default notebook to " << notebookUid;
    }

    // Each resource is either a single event series (or non-recurring event) OR
    // a list of updated/added persistent exceptions to an existing series.
    // If the resource contains an event series which includes the base incidence,
    // then we need to compare the local series with the remote series, to ensure
    // we remove any incidences which occur locally but not remotely.
    // However, if the resource's incidence list does not contain the base incidence,
    // but instead contains just persistent exceptions (ie, have recurrenceId) then
    // we can assume that no persistent exceptions were removed - only added/updated.
    // find the recurring incidence (parent) in the update list, and save it.
    // alternatively, it may be a non-recurring base incidence.
    const QString remoteUid = incidences.first()->uid();
    const QString &nbuid = nbUid(notebookUid, remoteUid);
    // Load from storage any matching incidence by uid or modified uid.
    mStorage->load(remoteUid);
    mStorage->load(nbuid);
    KCalendarCore::Incidence::Ptr localBaseIncidence
        = mStorage->calendar()->incidence(remoteUid);
    if (!localBaseIncidence) {
        localBaseIncidence = mStorage->calendar()->incidence(nbuid);
    }
    const QString localUid = localBaseIncidence ? localBaseIncidence->uid() : nbuid;
    qCDebug(lcCalDav) << "Saving the remote resource:" << resource.href;

    const QDateTime syncDateTime = notebook->syncDate();
    KCalendarCore::Incidence::Ptr remoteBaseIncidence;
    for (KCalendarCore::Incidence::Ptr incidence : incidences) {
        if (!incidence || incidence->uid() != remoteUid) {
            qCWarning(lcCalDav) << "Updated incidence list contains incidences with non-matching uids!";
            if (errorMessage)
                *errorMessage = "Non-matching UIDs in ICS data.";
            return false; // this is always an error.  each resource corresponds to a single event series.
        }
        if (!incidence->hasRecurrenceId()) {
            remoteBaseIncidence = incidence;
        }
        // Update received incidences.
        incidence->setUid(localUid);
        IncidenceHandler::setUri(incidence, resource.href);
        IncidenceHandler::setEtag(incidence, resource.etag);
        if (incidence->hasRecurrenceId()) {
            IncidenceHandler::setSyncedException(incidence);
        }
        // To avoid spurious appearings of added events when later
        // calling addedIncidences() and modifiedIncidences(), we
        // set the creation date and modification date by hand, since
        // libical has put them to now when they don't exist.
        if (incidence->created() > syncDateTime) {
            incidence->setCreated(syncDateTime.addSecs(-2));
        }
        if (incidence->lastModified() > syncDateTime) {
            incidence->setLastModified(syncDateTime.addSecs(-2));
        }
    }

    qCDebug(lcCalDav) << "Saving the added/updated base incidence:" << remoteUid;
    if (localBaseIncidence) {
        if (remoteBaseIncidence) {
            updateIncidence(remoteBaseIncidence, localBaseIncidence);
            if (modifications)
                modifications->append(localBaseIncidence);
        }
    } else {
        if (!remoteBaseIncidence) {
            // construct a recurring parent series for these orphans.
            localBaseIncidence = KCalendarCore::Incidence::Ptr(incidences.first()->clone());
            localBaseIncidence->setRecurrenceId(QDateTime());
        } else {
            localBaseIncidence = remoteBaseIncidence;
        }

        qCDebug(lcCalDav) << "Adding new incidence:" << localBaseIncidence->uid();
        if (mStorage->calendar()->addIncidence(localBaseIncidence)) {
            localBaseIncidence = mStorage->calendar()->incidence(localBaseIncidence->uid());
            if (additions && remoteBaseIncidence)
                additions->append(localBaseIncidence);
        } else {
            localBaseIncidence.clear();
        }
    }
    if (!localBaseIncidence) {
        qCWarning(lcCalDav) << "Error saving base incidence of resource.";
        if (errorMessage)
            *errorMessage = "Cannot create local parent.";
        return false;
    }

    bool completed = true;
    // update persistent exceptions which are in the remote list.
    QList<QDateTime> remoteRecurrenceIds;
    for (KCalendarCore::Incidence::Ptr remoteInstance : incidences) {
        if (!remoteInstance->hasRecurrenceId()) {
            continue; // already handled this one.
        }
        remoteRecurrenceIds.append(remoteInstance->recurrenceId());

        qCDebug(lcCalDav) << "Now saving a persistent exception:"
                          << remoteInstance->recurrenceId();
        KCalendarCore::Incidence::Ptr localInstance = mStorage->calendar()->incidence(remoteInstance->uid(), remoteInstance->recurrenceId());
        if (localInstance) {
            updateIncidence(remoteInstance, localInstance);
            if (modifications)
                modifications->append(localInstance);
        } else {
            if (!remoteBaseIncidence) {
                if (localBaseIncidence->allDay()
                    && !localBaseIncidence->recursOn(remoteInstance->recurrenceId().date(),
                                                     remoteInstance->recurrenceId().timeZone())) {
                    localBaseIncidence->recurrence()->addRDate(remoteInstance->recurrenceId().date());
                } else if (!localBaseIncidence->allDay()
                           && !localBaseIncidence->recursAt(remoteInstance->recurrenceId())) {
                    localBaseIncidence->recurrence()->addRDateTime(remoteInstance->recurrenceId());
                }
            }
            if (!mStorage->calendar()->addIncidence(remoteInstance)) {
                qCWarning(lcCalDav) << "Error saving occurrence.";
                if (errorMessage)
                    *errorMessage = "Cannot create exception.";
                completed = false;
            } else if (additions) {
                additions->append(remoteInstance);
            }
        }
    }

    // remove persistent exceptions which are not in the remote list.
    // only when the remote list contains the recurring incidence.
    KCalendarCore::Incidence::List localInstances;
    if (localBaseIncidence->recurs() && remoteBaseIncidence)
        localInstances = mStorage->calendar()->instances(localBaseIncidence);
    for (int i = 0; i < localInstances.size(); ++i) {
        KCalendarCore::Incidence::Ptr localInstance = localInstances[i];
        if (!remoteRecurrenceIds.contains(localInstance->recurrenceId())) {
            qCDebug(lcCalDav) << "Deleting occurrence:"
                              << localInstance->recurrenceId();
            if (!mStorage->calendar()->deleteIncidence(localInstance)) {
                qCWarning(lcCalDav) << "Unable to delete occurrence";
                if (errorMessage)
                    *errorMessage = "Cannot delete exception.";
                completed = false;
            }
        }
    }

    return completed;
}

template<void (*Flag)(const KCalendarCore::Incidence::Ptr&)>
void applyFlag(mKCal::ExtendedStorage::Ptr storage,
               const QStringList &uids)
{
    for (const QString &uid : uids) {
        if (storage->load(uid)) {
            KCalendarCore::Incidence::Ptr incidence
                = storage->calendar()->incidence(uid);
            if (incidence) {
                (*Flag)(incidence);
                const KCalendarCore::Incidence::List instances = storage->calendar()->instances(incidence);
                for (const KCalendarCore::Incidence::Ptr &instance : instances) {
                    (*Flag)(instance);
                }
            }
        }
    }
}

void LocalStorage::flagUpdateFailures(const QStringList &uids)
{
    applyFlag<&IncidenceHandler::flagUpdateFailure>(mStorage, uids);
}

void LocalStorage::flagUploadFailures(const QStringList &uids)
{
    applyFlag<&IncidenceHandler::flagUploadFailure>(mStorage, uids);
}

bool LocalStorage::deleteInstance(const QString &uid, const QDateTime &recurrenceId)
{
    if (mStorage->load(uid)) {
        KCalendarCore::Incidence::Ptr doomed
            = mStorage->calendar()->incidence(uid, recurrenceId);
        if (doomed && !mStorage->calendar()->deleteIncidence(doomed)) {
            IncidenceHandler::flagDeleteFailure(doomed);
            return false;
        }
    }
    return true;
}

bool LocalStorage::purgeDeleted(const QString &notebookUid, const KCalendarCore::Incidence::List &incidences)
{
    if (!incidences.isEmpty() && !mStorage->purgeDeletedIncidences(incidences,
                                                                   notebookUid)) {
        qCWarning(lcCalDav) << "Cannot purge from database the marked as deleted incidences.";
        return false;
    }
    return true;
}

void LocalStorage::resourceIsSynced(const QString &uri, const QString &etag, const QString &uid)
{
    if (!mStorage->load(uid)) {
        qCWarning(lcCalDav) << "Unable to load incidence from database:" << uid;
        return;
    }

    KCalendarCore::Incidence::Ptr localBaseIncidence
        = mStorage->calendar()->incidence(uid);
    if (localBaseIncidence) {
        localBaseIncidence->startUpdates();
        IncidenceHandler::setUri(localBaseIncidence, uri);
        IncidenceHandler::setEtag(localBaseIncidence, etag);
        IncidenceHandler::flagSuccess(localBaseIncidence);
        localBaseIncidence->endUpdates();
        if (localBaseIncidence->recurs()) {
            const KCalendarCore::Incidence::List instances = mStorage->calendar()->instances(localBaseIncidence);
            for (const KCalendarCore::Incidence::Ptr &instance : instances) {
                instance->startUpdates();
                IncidenceHandler::setUri(instance, uri);
                IncidenceHandler::setEtag(instance, etag);
                IncidenceHandler::setSyncedException(instance);
                IncidenceHandler::flagSuccess(instance);
                instance->endUpdates();
            }
        }
    } else {
        qCWarning(lcCalDav) << "Unable to find base incidence: " << uid;
    }
}

bool LocalStorage::save()
{
    return mStorage->save(mKCal::ExtendedStorage::PurgeDeleted);
}
