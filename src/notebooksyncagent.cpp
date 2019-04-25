/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2014 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Bea Lam <bea.lam@jollamobile.com>
 *               Stephan Rave <mail@stephanrave.de>
 *               Chris Adams <chris.adams@jollamobile.com>
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
#include "notebooksyncagent.h"
#include "incidencehandler.h"
#include "settings.h"
#include "report.h"
#include "put.h"
#include "delete.h"
#include "reader.h"

#include <LogMacros.h>
#include <SyncResults.h>

#include <memorycalendar.h>
#include <incidence.h>
#include <icalformat.h>
#include <event.h>
#include <todo.h>
#include <journal.h>
#include <attendee.h>

#include <QUuid>
#include <QDebug>


#define NOTEBOOK_FUNCTION_CALL_TRACE FUNCTION_CALL_TRACE(QString("%1 %2").arg(Q_FUNC_INFO).arg(mNotebook ? mNotebook->account() : ""))

namespace {
    // mKCal deletes custom properties of deleted incidences.
    // This is problematic for sync, as we need some fields
    // (resource URI and ETAG) in order to sync properly.
    // Hence, we abuse the COMMENTS field of the incidence.
    QString incidenceHrefUri(KCalCore::Incidence::Ptr incidence, const QString &remoteCalendarPath = QString(), bool *uriNeedsFilling = 0)
    {
        const QStringList &comments(incidence->comments());
        Q_FOREACH (const QString &comment, comments) {
            if (comment.startsWith("buteo:caldav:uri:")) {
                QString uri = comment.mid(17);
                if (uri.contains('%')) {
                    // if it contained a % or a space character, we percent-encoded
                    // the uri before storing it, because otherwise kcal doesn't
                    // split the comments properly.
                    uri = QUrl::fromPercentEncoding(uri.toUtf8());
                    LOG_DEBUG("URI comment was percent encoded:" << comment << ", returning uri:" << uri);
                }
                return uri;
            }
        }
        if (uriNeedsFilling) {
            // must be a newly locally-added event, with uri comment not yet set.
            // return the value which we should upload the event to.
            *uriNeedsFilling = true;
            return remoteCalendarPath + incidence->uid() + ".ics";
        }
        LOG_WARNING("Returning empty uri for:" << incidence->uid() << incidence->recurrenceId().toString());
        return QString();
    }
    void setIncidenceHrefUri(KCalCore::Incidence::Ptr incidence, const QString &hrefUri)
    {
        const QStringList &comments(incidence->comments());
        Q_FOREACH (const QString &comment, comments) {
            if (comment.startsWith("buteo:caldav:uri:")) {
                incidence->removeComment(comment);
                break;
            }
        }
        if (hrefUri.contains('%') || hrefUri.contains(' ')) {
            // need to percent-encode the uri before storing it,
            // otherwise mkcal doesn't split the comments correctly.
            incidence->addComment(QStringLiteral("buteo:caldav:uri:%1").arg(QString::fromUtf8(QUrl::toPercentEncoding(hrefUri))));
        } else {
            incidence->addComment(QStringLiteral("buteo:caldav:uri:%1").arg(hrefUri));
        }
    }
    QString incidenceETag(KCalCore::Incidence::Ptr incidence)
    {
        const QStringList &comments(incidence->comments());
        Q_FOREACH (const QString &comment, comments) {
            if (comment.startsWith("buteo:caldav:etag:")) {
                return comment.mid(18);
            }
        }
        return QString();
    }
    void setIncidenceETag(KCalCore::Incidence::Ptr incidence, const QString &etag)
    {
        const QStringList &comments(incidence->comments());
        Q_FOREACH (const QString &comment, comments) {
            if (comment.startsWith("buteo:caldav:etag:")) {
                incidence->removeComment(comment);
                break;
            }
        }
        incidence->addComment(QStringLiteral("buteo:caldav:etag:%1").arg(etag));
    }

    void updateIncidenceHrefEtag(KCalCore::Incidence::Ptr incidence,
                                 const QString &href, const QString &etag)
    {
        // Set the URI and the ETAG property to the required values.
        LOG_DEBUG("Adding URI and ETAG to incidence:" << incidence->uid() << incidence->recurrenceId().toString() << ":" << href << etag);
        if (!href.isEmpty())
            setIncidenceHrefUri(incidence, href);
        if (!etag.isEmpty())
            setIncidenceETag(incidence, etag);
        if (incidence->recurrenceId().isValid()) {
            // The persistent exception is now on server also.
            incidence->removeComment("buteo:caldav:detached-and-synced");
            incidence->addComment("buteo:caldav:detached-and-synced");
        }
    }

    bool isCopiedDetachedIncidence(KCalCore::Incidence::Ptr incidence)
    {
        if (incidence->recurrenceId().isNull())
            return false;

        const QStringList &comments(incidence->comments());
        Q_FOREACH (const QString &comment, comments) {
            if (comment == "buteo:caldav:detached-and-synced") {
                return false;
            }
        }
        return true;
    }
    KCalCore::Incidence::Ptr setExceptionIncidence(mKCal::ExtendedCalendar::Ptr calendar,
                                                   KCalCore::Incidence::Ptr recurringIncidence,
                                                   KCalCore::Incidence::Ptr exception)
    {
        KCalCore::Incidence::Ptr occurrence =
            calendar->dissociateSingleOccurrence(recurringIncidence,
                                                 exception->recurrenceId(),
                                                 exception->recurrenceId().timeSpec());
        if (occurrence.isNull()) {
            return occurrence;
        }

        IncidenceHandler::prepareImportedIncidence(exception);
        IncidenceHandler::copyIncidenceProperties(occurrence, exception);
        // Add a flag to distinguish persistent exceptions that have
        // been detached during the sync process (with the flag)
        // or by a call to dissociateSingleOccurrence() outside
        // of the sync process (in that later case, the incidence
        // will have to be treated as a local addition of a persistent
        // exception, see the calculateDelta() function).
        occurrence->addComment("buteo:caldav:detached-and-synced");
        return occurrence;
    }
}


NotebookSyncAgent::NotebookSyncAgent(mKCal::ExtendedCalendar::Ptr calendar,
                                     mKCal::ExtendedStorage::Ptr storage,
                                     QNetworkAccessManager *networkAccessManager,
                                     Settings *settings,
                                     const QString &remoteCalendarPath,
                                     QObject *parent)
    : QObject(parent)
    , mNetworkManager(networkAccessManager)
    , mSettings(settings)
    , mCalendar(calendar)
    , mStorage(storage)
    , mNotebook(0)
    , mRemoteCalendarPath(remoteCalendarPath)
    , mSyncMode(NoSyncMode)
    , mRetriedReport(false)
    , mNotebookNeedsDeletion(false)
    , mFinished(false)
{
}

NotebookSyncAgent::~NotebookSyncAgent()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    clearRequests();
}

void NotebookSyncAgent::abort()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    clearRequests();
}

void NotebookSyncAgent::clearRequests()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    QList<Request *> requests = mRequests.toList();
    for (int i=0; i<requests.count(); i++) {
        QObject::disconnect(requests[i], 0, this, 0);
        requests[i]->deleteLater();
    }
    mRequests.clear();
}

bool NotebookSyncAgent::setNotebookFromInfo(const QString &notebookName,
                                            const QString &color,
                                            const QString &accountId,
                                            const QString &pluginName,
                                            const QString &syncProfile)
{
    mNotebook = static_cast<mKCal::Notebook::Ptr>(0);
    // Look for an already existing notebook in storage for this account and path.
    Q_FOREACH (mKCal::Notebook::Ptr notebook, mStorage->notebooks()) {
        if (notebook->account() == accountId
            && notebook->syncProfile().endsWith(QStringLiteral(":%1").arg(mRemoteCalendarPath))) {
            LOG_DEBUG("found notebook:" << notebook->uid() << "for remote calendar:" << mRemoteCalendarPath);
            if (!mStorage->loadNotebookIncidences(notebook->uid()))
                return false;
            mNotebook = notebook;
            mNotebook->setColor(color);
            mNotebook->setName(notebookName);
            return true;
        }
    }
    LOG_DEBUG("no notebook exists for" << mRemoteCalendarPath);
    // or create a new one
    mNotebook = mKCal::Notebook::Ptr(new mKCal::Notebook(notebookName, QString()));
    mNotebook->setAccount(accountId);
    mNotebook->setPluginName(pluginName);
    mNotebook->setSyncProfile(syncProfile + ":" + mRemoteCalendarPath); // ugly hack because mkcal API is deficient.  I wanted to use uid field but it won't save.
    mNotebook->setColor(color);
    return true;
}

void NotebookSyncAgent::startSync(const QDateTime &fromDateTime,
                                  const QDateTime &toDateTime)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (!mNotebook) {
        LOG_DEBUG("no notebook to sync.");
        return;
    }

    // Store sync time before sync is completed to avoid loosing events
    // that may be inserted server side between now and the termination
    // of the process.
    mNotebookSyncedDateTime = KDateTime::currentUtcDateTime();
    mFromDateTime = fromDateTime;
    mToDateTime = toDateTime;
    if (mNotebook->syncDate().isNull()) {
/*
    Slow sync mode:

    1) Get all calendars on the server using Report::getAllEvents()
    2) Save all received calendar data to disk.

    Step 2) is triggered by CalDavClient once *all* notebook syncs have finished.
 */
        LOG_DEBUG("Start slow sync for notebook:" << mNotebook->name() << "for account" << mNotebook->account()
                  << "between" << fromDateTime << "to" << toDateTime);

        mSyncMode = SlowSync;

        sendReportRequest();
    } else {
/*
    Quick sync mode:

    1) Get all remote calendar etags and updated calendar data from the server using Report::getAllETags()
    2) Get all local changes since the last sync
    3) Filter out local changes that were actually remote changes written by step 5) of this
       sequence from a previous sync
    4) Send the local changes to the server using Put and Delete requests
    5) Write the remote calendar changes to disk.

    Step 5) is triggered by CalDavClient once *all* notebook syncs have finished.
 */
        LOG_DEBUG("Start quick sync for notebook:" << mNotebook->uid()
                  << "between" << fromDateTime << "to" << toDateTime
                  << ", sync changes since" << mNotebook->syncDate().dateTime());
        mSyncMode = QuickSync;

        fetchRemoteChanges();
    }
}

void NotebookSyncAgent::sendReportRequest()
{
    // must be m_syncMode = SlowSync.
    Report *report = new Report(mNetworkManager, mSettings);
    mRequests.insert(report);
    connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
    report->getAllEvents(mRemoteCalendarPath, mFromDateTime, mToDateTime);
}

void NotebookSyncAgent::fetchRemoteChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // must be m_syncMode = QuickSync.
    Report *report = new Report(mNetworkManager, mSettings);
    mRequests.insert(report);
    connect(report, SIGNAL(finished()), this, SLOT(processETags()));
    report->getAllETags(mRemoteCalendarPath, mFromDateTime, mToDateTime);
}

void NotebookSyncAgent::reportRequestFinished()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = qobject_cast<Report*>(sender());
    mRequests.remove(report);
    report->deleteLater();

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        if (mPossibleLocalModificationIncidenceIds.isEmpty()) {
            // every received resource was from m_remoteAdditions or m_remoteModifications.
            mReceivedCalendarResources = report->receivedCalendarResources();
        } else {
            // From the received resources, some will have been fetched so that we can perform a more detailed
            // per-event delta calculation (ie, the "possible" local modifications).
            // We can discard any incidence in the m_localModifications list which is not significantly
            // different to the incidence fetched from the remote server.
            // In either case, we will remove that resource from the receivedResources list because
            // we don't want to store the remote version of it (instead we probably want to upsync the local mod).
            int originalLocalModificationsCount = mLocalModifications.size(), discardedLocalModifications = 0;
            Q_FOREACH (const Reader::CalendarResource &resource,
                       report->receivedCalendarResources()) {
                if (mPossibleLocalModificationIncidenceIds.contains(resource.href)) {
                    // this resource was fetched so that we could perform a field-by-field delta detection
                    // just in case the only change was the ETAG/URI value (due to previous sync).
                    removePossibleLocalModificationIfIdentical(mPossibleLocalModificationIncidenceIds.values(resource.href),
                                                               resource,
                                                               &mLocalModifications);
                } else {
                    // these were resources fetched from m_remoteAdditions or m_remoteModifications
                    mReceivedCalendarResources.append(resource);
                }
            }
            discardedLocalModifications = originalLocalModificationsCount - mLocalModifications.size();
            LOG_DEBUG("" << discardedLocalModifications << "out of" << originalLocalModificationsCount <<
                      "local modifications were discarded as spurious (etag/uri update only)");
        }

        LOG_DEBUG("Report request finished: received:"
                  << report->receivedCalendarResources().length() << "iCal blobs containing a total of"
                  << report->receivedCalendarResources().count() << "incidences"
                  << "of which" << mReceivedCalendarResources.size() << "incidences were remote additions/modifications");

        if (mSyncMode == QuickSync) {
            sendLocalChanges();
            return;
        }

        // NOTE: we don't store the remote artifacts yet
        // Instead, we just emit finished (for this notebook)
        // Once ALL notebooks are finished, then we apply the remote changes.
        // This prevents the worst partial-sync issues.

    } else if (mSyncMode == SlowSync
               && report->networkError() == QNetworkReply::AuthenticationRequiredError
               && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        LOG_WARNING("Retrying REPORT after request failed with QNetworkReply::AuthenticationRequiredError");
        mRetriedReport = true;
        sendReportRequest();
        return;
    } else if (mSyncMode == SlowSync
               && report->networkError() == QNetworkReply::ContentNotFoundError) {
        // The remote calendar resource was removed after we created the account but before first sync.
        // We don't perform resource discovery in CalDAV during each sync cycle,
        // so we can have local calendar metadata for remotely removed calendars.
        // In this case, we just skip sync of this calendar, as it was deleted.
        mNotebookNeedsDeletion = true;
        LOG_DEBUG("calendar" << mRemoteCalendarPath << "was deleted remotely, skipping sync locally.");
        emitFinished(Buteo::SyncResults::NO_ERROR, QString());
        return;
    }

    LOG_DEBUG("emitting report request finished with result:" << report->errorCode() << report->errorString());
    emitFinished(report->errorCode(), report->errorString());
}

void NotebookSyncAgent::processETags()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = qobject_cast<Report*>(sender());
    mRequests.remove(report);
    report->deleteLater();

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        LOG_DEBUG("Process tags for server path" << mRemoteCalendarPath);
        // we have a hash from resource href-uri to resource info (including etags).
        QHash<QString, QString> remoteHrefUriToEtags;
        Q_FOREACH (const Reader::CalendarResource &resource,
                   report->receivedCalendarResources()) {
            if (!resource.href.contains(mRemoteCalendarPath)) {
                LOG_WARNING("href does not contain server path:" << resource.href << ":" << mRemoteCalendarPath);
                emitFinished(Buteo::SyncResults::INTERNAL_ERROR, "unable to calculate remote resource uids");
                return;
            }
            remoteHrefUriToEtags.insert(resource.href, resource.etag);
        }

        // calculate the local and remote delta.
        if (!calculateDelta(remoteHrefUriToEtags,
                            &mLocalAdditions,
                            &mLocalModifications,
                            &mLocalDeletions,
                            &mRemoteAdditions,
                            &mRemoteModifications,
                            &mRemoteDeletions)) {
            LOG_WARNING("unable to calculate the sync delta for:" << mRemoteCalendarPath);
            emitFinished(Buteo::SyncResults::INTERNAL_ERROR, "unable to calculate sync delta");
            return;
        }

        // Note that due to the fact that we update the ETAG and URI data in locally
        // upsynced events during sync, those incidences will be reported as modified
        // during the next sync cycle (even though the only changes may have been
        // that ETAG+URI change).  Hence, we need to fetch all of those again, and
        // then manually check equivalence (ignoring etag+uri value) with remote copy.
        // Also fetch updated and new items full data if required.
        QStringList fetchRemoteHrefUris = mRemoteAdditions + mRemoteModifications + mPossibleLocalModificationIncidenceIds.uniqueKeys();
        if (!fetchRemoteHrefUris.isEmpty()) {
            // some incidences have changed on the server, so fetch the new details
            Report *report = new Report(mNetworkManager, mSettings);
            mRequests.insert(report);
            connect(report, SIGNAL(finished()), this, SLOT(reportRequestFinished()));
            report->multiGetEvents(mRemoteCalendarPath, fetchRemoteHrefUris);
            return;
        }

        // no remote modifications/additions we need to fetch; just upsync local changes.
        sendLocalChanges();
        return;
    } else if (report->networkError() == QNetworkReply::AuthenticationRequiredError && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        LOG_WARNING("Retrying ETAG REPORT after request failed with QNetworkReply::AuthenticationRequiredError");
        mRetriedReport = true;
        fetchRemoteChanges();
        return;
    } else if (report->networkError() == QNetworkReply::ContentNotFoundError) {
        // The remote calendar resource was removed.
        // We don't perform resource discovery in CalDAV during each sync cycle,
        // so we can have local calendars which mirror remotely-removed calendars.
        // In this situation, we need to delete the local calendar.
        mNotebookNeedsDeletion = true;
        LOG_DEBUG("calendar" << mRemoteCalendarPath << "was deleted remotely, marking for deletion locally:" << mNotebook->name());
        emitFinished(Buteo::SyncResults::NO_ERROR, QString());
        return;
    }

    // no remote changes to downsync, and no local changes to upsync - we're finished.
    LOG_DEBUG("no remote changes to downsync and no local changes to upsync - finished!");
    emitFinished(report->errorCode(), report->errorString());
}

void NotebookSyncAgent::sendLocalChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (!mLocalAdditions.count() && !mLocalModifications.count() && !mLocalDeletions.count()) {
        // no local changes to upsync.
        // we're finished syncing.
        LOG_DEBUG("no local changes to upsync - finished with notebook" << mNotebook->name() << mRemoteCalendarPath);
        emitFinished(Buteo::SyncResults::NO_ERROR, QString());
        return;
    } else {
        LOG_DEBUG("upsyncing local changes: A/M/R:" << mLocalAdditions.count() << "/" << mLocalModifications.count() << "/" << mLocalDeletions.count());
    }

    bool localChangeRequestStarted = false;
    // For deletions, if a persistent exception is deleted we may need to do a PUT
    // containing all of the still-existing events in the series.
    // (Alternative is to push a STATUS:CANCELLED event?)
    // Hence, we first need to find out if any deletion is a lone-persistent-exception deletion.
    QMultiHash<QString, KDateTime> uidToRecurrenceIdDeletions;
    QHash<QString, QString> uidToUri;  // we cannot look up custom properties of deleted incidences, so cache them here.
    Q_FOREACH (KCalCore::Incidence::Ptr localDeletion, mLocalDeletions) {
        uidToRecurrenceIdDeletions.insert(localDeletion->uid(), localDeletion->recurrenceId());
        uidToUri.insert(localDeletion->uid(), incidenceHrefUri(localDeletion));
    }

    // now send DELETEs as required, and PUTs as required.
    Q_FOREACH (const QString &uid, uidToRecurrenceIdDeletions.uniqueKeys()) {
        QList<KDateTime> recurrenceIds = uidToRecurrenceIdDeletions.values(uid);
        if (!recurrenceIds.contains(KDateTime())) {
            KCalCore::Incidence::Ptr recurringSeries = mCalendar->incidence(uid);
            if (!recurringSeries.isNull()) {
                mLocalModifications.append(recurringSeries);
                continue; // finished with this deletion.
            } else {
                LOG_WARNING("Unable to load recurring incidence for deleted exception; deleting entire series instead");
                // fall through to the DELETE code below.
            }
        }

        // the whole series is being deleted; can DELETE.
        localChangeRequestStarted = true;
        QString remoteUri = uidToUri.value(uid);
        LOG_DEBUG("deleting whole series:" << remoteUri << "with uid:" << uid);
        Delete *del = new Delete(mNetworkManager, mSettings);
        mRequests.insert(del);
        connect(del, &Delete::finished, this, &NotebookSyncAgent::nonReportRequestFinished);
        del->deleteEvent(remoteUri);
    }

    mSentUids.clear();
    KCalCore::Incidence::List toUpload(mLocalAdditions + mLocalModifications);
    for (int i = 0; i < toUpload.count(); i++) {
        bool create = false;
        QString href = incidenceHrefUri(toUpload[i], mRemoteCalendarPath, &create);
        if (href.isEmpty()) {
            emitFinished(Buteo::SyncResults::INTERNAL_ERROR,
                         "Unable to determine remote uri for incidence:" + toUpload[i]->uid());
            return;
        }
        if (mSentUids.contains(href)) {
            LOG_DEBUG("Already handled upload" << i << "via series update");
            continue; // already handled this one, as a result of a previous update of another occurrence in the series.
        }
        QString icsData = constructLocalChangeIcs(toUpload[i]);
        if (icsData.isEmpty()) {
            LOG_DEBUG("Skipping upload of broken incidence:" << i << ":" << toUpload[i]->uid());
        } else {
            LOG_DEBUG("Uploading incidence" << i << "via PUT for uid:" << toUpload[i]->uid());
            localChangeRequestStarted = true;
            Put *put = new Put(mNetworkManager, mSettings);
            mRequests.insert(put);
            connect(put, &Put::finished, this, &NotebookSyncAgent::nonReportRequestFinished);
            put->sendIcalData(href, icsData, incidenceETag(toUpload[i]));
            mSentUids.insert(href, toUpload[i]->uid());
        }
    }

    if (!localChangeRequestStarted) {
        LOG_WARNING("local change upsync skipped due to bad data - finished with notebook" << mNotebook->name() << mRemoteCalendarPath);
        emitFinished(Buteo::SyncResults::NO_ERROR, QString());
    }
}

void NotebookSyncAgent::nonReportRequestFinished()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Request *request = qobject_cast<Request*>(sender());
    if (!request) {
        LOG_WARNING("Report request finished but request is invalid, aborting sync for calendar:" << mRemoteCalendarPath);
        emitFinished(Buteo::SyncResults::INTERNAL_ERROR, QStringLiteral("Invalid request object"));
        return;
    }
    mRequests.remove(request);

    if (request->errorCode() != Buteo::SyncResults::NO_ERROR) {
        LOG_WARNING("Aborting sync," << request->command() << "failed" << request->errorString() << "for notebook" << mRemoteCalendarPath << "of account:" << mNotebook->account());
        emitFinished(request->errorCode(), request->errorString());
    } else {
        Put *putRequest = qobject_cast<Put*>(request);
        if (putRequest) {
            // Apply Etag and Href changes immediately since incidences are now
            // for sure on server.
            for (QHash<QString, QString>::ConstIterator
                     it = putRequest->updatedETags().constBegin();
                 it != putRequest->updatedETags().constEnd(); ++it) {
                if (mSentUids.contains(it.key())) {
                    updateHrefETag(mSentUids.take(it.key()), it.key(), it.value());
                }
            }
        }
        if (mRequests.isEmpty()) {
            finalizeSendingLocalChanges();
        }
    }
    request->deleteLater();
}

void NotebookSyncAgent::finalizeSendingLocalChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // All PUT requests have been finished, and mSentUids have been cleared from
    // uids that have already been updated with new etag value. Just remains
    // the ones that requires additional retrieval to get etag values.
    if (!mSentUids.isEmpty()) {
        Report *report = new Report(mNetworkManager, mSettings);
        mRequests.insert(report);
        connect(report, SIGNAL(finished()), this, SLOT(additionalReportRequestFinished()));
        report->multiGetEtags(mRemoteCalendarPath, mSentUids.keys());
        return;
    } else {
        emitFinished(Buteo::SyncResults::NO_ERROR, QStringLiteral("Finished requests for %1").arg(mNotebook->account()));
    }
}

void NotebookSyncAgent::additionalReportRequestFinished()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // The server did not originally respond with the update ETAG values after
    // our initial PUT/UPDATE so we had to do an addition report request.
    // This response will contain the new ETAG values for any resource we
    // upsynced (ie, a local modification/addition).

    Report *report = qobject_cast<Report*>(sender());
    mRequests.remove(report);
    report->deleteLater();

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        LOG_DEBUG("Additional report request finished: received:"
                  << report->receivedCalendarResources().length() << "iCal blobs containing a total of"
                  << report->receivedCalendarResources().count() << "incidences");
        for (QList<Reader::CalendarResource>::ConstIterator
                 it = report->receivedCalendarResources().constBegin();
             it != report->receivedCalendarResources().constEnd(); ++it) {
            if (mSentUids.contains(it->href)) {
                updateHrefETag(mSentUids.take(it->href), it->href, it->etag);
            }
        }
        LOG_DEBUG("Remains" << mSentUids.count() << "uris not updated.");
        emitFinished(Buteo::SyncResults::NO_ERROR, QStringLiteral("Finished requests for %1").arg(mNotebook->account()));
        return;
    }

    LOG_WARNING("Additional report request finished with error, aborting sync of notebook:" << mRemoteCalendarPath);
    emitFinished(report->errorCode(), report->errorString());
}

bool NotebookSyncAgent::applyRemoteChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (!mNotebook) {
        LOG_DEBUG("Missing notebook in apply changes.");
        return false;
    }

    if (mSyncMode == SlowSync) {
        if (mNotebookNeedsDeletion) {
            // the calendar was deleted remotely prior to being synced locally.
            // simply don't create a local notebook for the nonexistent calendar.
            return true;
        }

        // If current notebook is not already in storage, we add it.
        if (!mStorage->notebook(mNotebook->uid())) {
            if (!mStorage->addNotebook(mNotebook)) {
                LOG_DEBUG("Unable to (re)create notebook" << mNotebook->name() << "during slow sync for account" << mNotebook->account() << ":" << mRemoteCalendarPath);
                return false;
            }
        }
    } else if (mNotebookNeedsDeletion) {
        // delete the notebook from local database
        mStorage->deleteNotebook(mNotebook);
        return true;
    }

    if (!updateIncidences(mReceivedCalendarResources)) {
        return false;
    }
    if (!deleteIncidences(mRemoteDeletions)) {
        return false;
    }

    mNotebook->setSyncDate(mNotebookSyncedDateTime);
    mStorage->updateNotebook(mNotebook);

    return true;
}

void NotebookSyncAgent::emitFinished(int minorErrorCode, const QString &message)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (mFinished) {
        return;
    }
    mFinished = true;
    clearRequests();

    emit finished(minorErrorCode, message);
}

void NotebookSyncAgent::finalize()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;
}

bool NotebookSyncAgent::isFinished() const
{
    return mFinished;
}

// ------------------------------ Utility / implementation functions.

// called in the QuickSync codepath after fetching etags for remote resources.
// from the etags, we can determine the local and remote sync delta.
bool NotebookSyncAgent::calculateDelta(
        // in parameters:
        const QHash<QString, QString> &remoteUriEtags, //  remoteEtags: map of uri to etag which exist on the remote server.
        // out parameters:
        KCalCore::Incidence::List *localAdditions,
        KCalCore::Incidence::List *localModifications,
        KCalCore::Incidence::List *localDeletions,
        QList<QString> *remoteAdditions,
        QList<QString> *remoteModifications,
        KCalCore::Incidence::List *remoteDeletions)
{
    // Note that the mKCal API doesn't provide a way to get all deleted/modified incidences
    // for a notebook, as it implements the SQL query using an inequality on both modifiedAfter
    // and createdBefore; so instead we have to build a datetime which "should" satisfy
    // the inequality for all possible local modifications detectable since the last sync.
    KDateTime syncDateTime = mNotebook->syncDate().addSecs(1); // deleted after, created before...

    // load all local incidences
    KCalCore::Incidence::List localIncidences;
    if (!mStorage->allIncidences(&localIncidences, mNotebook->uid())) {
        LOG_WARNING("Unable to load notebook incidences, aborting sync of notebook:" << mRemoteCalendarPath << ":" << mNotebook->uid());
        emitFinished(Buteo::SyncResults::DATABASE_FAILURE, QString("Unable to load storage incidences for notebook: %1").arg(mNotebook->uid()));
        return false;
    }

    // separate them into buckets.
    // note that each remote URI can be associated with multiple local incidences (due recurrenceId incidences)
    // Here we can determine local additions and remote deletions.
    QSet<QString> seenRemoteUris;
    QHash<QString, QString> previouslySyncedEtags; // remote uri to the etag we saw last time.
    Q_FOREACH (KCalCore::Incidence::Ptr incidence, localIncidences) {
        bool uriWasEmpty = false;
        QString remoteUri = incidenceHrefUri(incidence, mRemoteCalendarPath, &uriWasEmpty);
        if (uriWasEmpty) {
            // must be either a new local addition or a previously-upsynced local addition
            // if we failed to update its uri after the successful upsync.
            if (remoteUriEtags.contains(remoteUri)) { // we saw this on remote side...
                // previously partially upsynced, needs uri update.
                LOG_DEBUG("have previously partially upsynced local addition, needs uri update:" << remoteUri);
                seenRemoteUris.insert(remoteUri);
                // ensure that it will be seen as a remote modification and trigger download
                previouslySyncedEtags.insert(remoteUri, QStringLiteral("missing ETag"));
            } else { // it doesn't exist on remote side...
                // new local addition.
                LOG_DEBUG("have new local addition:" << incidence->uid() << incidence->recurrenceId().toString());
                localAdditions->append(incidence);
                // Note: if it was partially upsynced and then connection failed
                // and then removed remotely, then on next sync (ie, this one)
                // it will appear like a "new" local addition.  TODO: FIXME? How?
            }
        } else {
            // this is a previously-synced incidence with a remote uri,
            // OR a newly-added persistent occurrence to a previously-synced recurring series.
            if (!remoteUriEtags.contains(remoteUri)) {
                LOG_DEBUG("have remote deletion of previously synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                remoteDeletions->append(incidence);
            } else if (isCopiedDetachedIncidence(incidence)) {
                if (incidenceETag(incidence) == remoteUriEtags.value(remoteUri)) {
                    LOG_DEBUG("Found new locally-added persistent exception:" << incidence->uid() << incidence->recurrenceId().toString() << ":" << remoteUri);
                    localAdditions->append(incidence);
                } else {
                    LOG_DEBUG("ignoring new locally-added persistent exception to remotely modified incidence:" << incidence->uid() << incidence->recurrenceId().toString() << ":" << remoteUri);
                }
            } else {
                // this is a possibly modified or possibly unchanged, previously synced incidence.
                // we later check to see if it was modified server-side by checking the etag value.
                LOG_DEBUG("have possibly modified or possibly unchanged previously synced local incidence:" << remoteUri);
            }
            seenRemoteUris.insert(remoteUri);
            previouslySyncedEtags.insert(remoteUri, incidenceETag(incidence));
        }
    }

    // Now determine local deletions reported by mkcal since the last sync date.
    KCalCore::Incidence::List deleted;
    if (!mStorage->deletedIncidences(&deleted, syncDateTime, mNotebook->uid())) {
        LOG_WARNING("mKCal::ExtendedStorage::deletedIncidences() failed");
        return false;
    }
    Q_FOREACH (KCalCore::Incidence::Ptr incidence, deleted) {
        bool uriWasEmpty = false;
        QString remoteUri = incidenceHrefUri(incidence, mRemoteCalendarPath, &uriWasEmpty);
        if (remoteUriEtags.contains(remoteUri)) {
            if (uriWasEmpty) {
                // we originally upsynced this pure-local addition, but then connectivity was
                // lost before we updated the uid of it locally to include the remote uri.
                // subsequently, the user deleted the incidence.
                // Hence, it exists remotely, and has been deleted locally.
                LOG_DEBUG("have local deletion for partially synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                // We treat this as a local deletion.
                setIncidenceHrefUri(incidence, remoteUri);
                setIncidenceETag(incidence, remoteUriEtags.value(remoteUri));
                localDeletions->append(incidence);
            } else {
                if (incidenceETag(incidence) == remoteUriEtags.value(remoteUri)) {
                    // the incidence was previously synced successfully.  it has now been deleted locally.
                    LOG_DEBUG("have local deletion for previously synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                    localDeletions->append(incidence);
                } else {
                    // Sub-optimal case for persistent exceptions.
                    // TODO: improve handling of this case.
                    LOG_DEBUG("ignoring local deletion due to remote modification:"
                              << incidence->uid() << incidence->recurrenceId().toString());
                }
            }
            seenRemoteUris.insert(remoteUri);
            previouslySyncedEtags.insert(remoteUri, incidenceETag(incidence));
        } else {
            // it was either already deleted remotely, or was never upsynced from the local prior to deletion.
            LOG_DEBUG("ignoring local deletion of non-existent remote incidence:" << incidence->uid() << incidence->recurrenceId().toString() << "at" << remoteUri);
        }
    }

    // Now determine local modifications.
    KCalCore::Incidence::List modified;
    if (!mStorage->modifiedIncidences(&modified, syncDateTime, mNotebook->uid())) {
        LOG_WARNING("mKCal::ExtendedStorage::modifiedIncidences() failed");
        return false;
    }
    Q_FOREACH (KCalCore::Incidence::Ptr incidence, modified) {
        // if it also appears in localDeletions, ignore it - it was deleted locally.
        // if it also appears in localAdditions, ignore it - we are already uploading it.
        // if it doesn't appear in remoteEtags, ignore it - it was deleted remotely.
        // if its etag has changed remotely, ignore it - it was modified remotely.
        bool uriWasEmpty = false;
        QString remoteUri = incidenceHrefUri(incidence, mRemoteCalendarPath, &uriWasEmpty);
        if (uriWasEmpty) {
            // incidence either hasn't been synced before, or was partially synced.
            if (remoteUriEtags.contains(remoteUri)) { // yep, we previously upsynced it but then connectivity died.
                // partially synced previously, connectivity died before we could update the uri field with remote url.
                LOG_DEBUG("have local modification to partially synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                // note: we cannot check the etag to determine if it changed, since we may not have received the updated etag after the partial sync.
                // we treat this as a "definite" local modification due to the partially-synced status.
                setIncidenceHrefUri(incidence, remoteUri);
                setIncidenceETag(incidence, remoteUriEtags.value(remoteUri));
                localModifications->append(incidence);
                previouslySyncedEtags.insert(remoteUri, incidenceETag(incidence));
                seenRemoteUris.insert(remoteUri);
            } else if (localAdditions->contains(incidence)) {
                LOG_DEBUG("ignoring local modification to locally added incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                continue;
            } else {
                LOG_DEBUG("ignoring local modification to remotely removed partially-synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                continue;
            }
        } else {
            // we have a modification to a previously-synced incidence.
            QString localEtag = incidenceETag(incidence);
            if (!remoteUriEtags.contains(remoteUri)) {
                LOG_DEBUG("ignoring local modification to remotely deleted incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                bool foundRemoteDeletion = false;
                for (int i = 0; i < remoteDeletions->size(); ++i) {
                    if (remoteDeletions->at(i)->uid() == incidence->uid() && remoteDeletions->at(i)->recurrenceId() == incidence->recurrenceId()) {
                        foundRemoteDeletion = true;
                        break;
                    }
                }
                if (!foundRemoteDeletion) {
                    // this should never happen, and is always an error.
                    LOG_WARNING("But unable to find corresponding remote deletion!  Aborting sync due to unrecoverable error!");
                    return false;
                }
            } else {
                // determine if the remote etag is still the same.
                // if it is not, then the incidence was modified server-side.
                if (localEtag != remoteUriEtags.value(remoteUri)) {
                    // if the etags are different, then the event was also modified remotely.
                    // we only support PreferRemote conflict resolution, so we discard the local modification.
                    LOG_DEBUG("ignoring local modification to remotely modified incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                    // Don't append it here, it will be appended later when treating remote modifications.
                    // remoteModifications->append(remoteUri);
                } else {
                    // this is either a real local modification, or a modification being reported due to URI/ETAG update after previous sync.
                    // because it may be being reported due solely to URI/ETAG update, we treat it as a "possible" local modification.
                    LOG_DEBUG("have possible local modification:" << incidence->uid() << incidence->recurrenceId().toString());
                    localModifications->append(incidence); // NOTE: we later reload this event from server to detect "true" delta.
                    mPossibleLocalModificationIncidenceIds.insert(remoteUri, incidence->recurrenceId());
                }
                seenRemoteUris.insert(remoteUri);
            }
        }
    }

    // now determine remote additions and modifications.
    Q_FOREACH (const QString &remoteUri, remoteUriEtags.keys()) {
        if (!seenRemoteUris.contains(remoteUri)) {
            // this is probably a pure server-side addition, but there is one other possibility:
            // if it was newly added to the server before the previous sync cycle, then it will
            // have been added locally (due to remote addition) during the last sync cycle.
            // If the event was subsequently deleted locally prior to this sync cycle, then
            // mKCal will NOT report it as a deletion (or an addition) because it assumes that
            // it was a pure local addition + deletion.
            // The solution?  We need to manually search every deleted incidence for uri value.
            // Unfortunately, the mKCal API doesn't allow us to get all deleted incidences,
            // but we can get all incidences deleted since the last sync date.
            // That should suffice, and we've already injected those deletions into the deletions
            // list, so if we hit this branch, then it must be a new remote addition.
            LOG_DEBUG("have new remote addition:" << remoteUri);
            remoteAdditions->append(remoteUri);
        } else if (!previouslySyncedEtags.contains(remoteUri)) {
            // this is a server-side modification which is obsoleted by a local deletion
            LOG_DEBUG("ignoring remote modification to locally deleted incidence at:" << remoteUri);
        } else if (previouslySyncedEtags.value(remoteUri) != remoteUriEtags.value(remoteUri)) {
            // etag changed; this is a server-side modification.
            LOG_DEBUG("have remote modification to previously synced incidence at:" << remoteUri);
            LOG_DEBUG("previously seen ETag was:" << previouslySyncedEtags.value(remoteUri) << "-> new ETag is:" << remoteUriEtags.value(remoteUri));
            remoteModifications->append(remoteUri);
        } else {
            // this incidence is unchanged since last sync.
            LOG_DEBUG("unchanged server-side since last sync:" << remoteUri);
        }
    }

    LOG_DEBUG("Calculated local  A/M/R:" << localAdditions->size() << "/" << localModifications->size() << "/" << localDeletions->size());
    LOG_DEBUG("Calculated remote A/M/R:" << remoteAdditions->size() << "/" << remoteModifications->size() << "/" << remoteDeletions->size());

    return true;
}

// Called in the QuickSync codepath after some local modifications were reported by mKCal.
// We fetched the remote version of the resource so that we can detect whether the local change
// is "real" or whether it was just reporting the local modification of the ETAG or URI field.
void NotebookSyncAgent::removePossibleLocalModificationIfIdentical(
        const QList<KDateTime> &recurrenceIds,
        const Reader::CalendarResource &remoteResource,
        KCalCore::Incidence::List *localModifications)
{
    // the remoteResource contains one ical resource fetched from the remote URI.
    Q_FOREACH (const KDateTime &rid, recurrenceIds) {
        // find the possible local modification associated with this recurrenceId.
        bool foundMatch = false;
        int removeIdx = -1;
        for (int i = 0; i < localModifications->size(); ++i) {
            // Only compare incidences which relate to the remote resource.
            QString hrefUri = incidenceHrefUri(localModifications->at(i));
            if (hrefUri != remoteResource.href) {
                LOG_DEBUG("skipping unrelated local modification:" << localModifications->at(i)->uid()
                          << "(" << hrefUri << ") for remote uri:" << remoteResource.href);
                continue;
            }
            if (localModifications->at(i)->recurrenceId() != rid) {
                continue;
            }
            // Note: we compare the remote resources with the "export" version of the local modifications
            // otherwise spurious differences might be detected.
            const KCalCore::Incidence::Ptr &pLMod = IncidenceHandler::incidenceToExport(localModifications->at(i), (localModifications->at(i)->recurs()) ? mCalendar->instances(localModifications->at(i)) : KCalCore::Incidence::List());
            // found the local incidence.  now find the copy received from the server and detect changes.
            Q_FOREACH (const KCalCore::Incidence::Ptr &remoteIncidence, remoteResource.incidences) {
                if (remoteIncidence->recurrenceId() == rid) {
                    const KCalCore::Incidence::Ptr &exportRInc = IncidenceHandler::incidenceToExport(remoteIncidence);
                    // found the remote incidence.  compare it to the local.
                    LOG_DEBUG("comparing:" << pLMod->uid() << "to" << exportRInc->uid() << "(" << remoteResource.href << ")");
                    foundMatch = true;
                    if (IncidenceHandler::copiedPropertiesAreEqual(pLMod, exportRInc)) {
                        removeIdx = i;  // this is a spurious local modification which needs to be removed.
                    } else {
                        removeIdx = -1; // this is a real local modification. no-op, but for completeness.
                    }
                    break;
                }
            }
            if (foundMatch) {
                break;
            }
        }

        // remove the possible local modification if it proved to be spurious
        if (foundMatch) {
            if (removeIdx >= 0) {
                LOG_DEBUG("discarding spurious local modification to:" << remoteResource.href << rid.toString());
                localModifications->remove(removeIdx);
            } else {
                LOG_DEBUG("local modification to:" << remoteResource.href << rid.toString() << "is real.");
            }
        } else {
            // this is always an internal logic error.  We explicitly requested it.
            LOG_WARNING("error: couldn't find remote incidence for possible local modification! FIXME!");
        }
    }
}

// A given incidence has been added or modified locally.
// To upsync the change, we need to construct the .ics data to upload to server.
// Since the incidence may be an occurrence or recurring series incidence,
// we cannot simply convert the incidence to iCal data, but instead we have to
// upsync an .ics containing the whole recurring series.
QString NotebookSyncAgent::constructLocalChangeIcs(KCalCore::Incidence::Ptr updatedIncidence)
{
    // create an in-memory calendar
    // add to it the required incidences (ie, check if has recurrenceId -> load parent and all instances; etc)
    // for each of those, we need to do the IncidenceToExport() modifications first
    // then, export from that calendar to .ics file.
    KCalCore::MemoryCalendar::Ptr memoryCalendar(new KCalCore::MemoryCalendar(KDateTime::UTC));
    if (updatedIncidence->hasRecurrenceId() || updatedIncidence->recurs()) {
        KCalCore::Incidence::Ptr recurringIncidence = updatedIncidence->hasRecurrenceId()
                                                ? mCalendar->incidence(updatedIncidence->uid(), KDateTime())
                                                : updatedIncidence;
        if (!recurringIncidence) {
            LOG_WARNING("Unable to find parent series for locally updated incidence:"
                        << updatedIncidence->uid() << ":" << updatedIncidence->recurrenceId().toString());
            return QString();
        }
        KCalCore::Incidence::List instances = mCalendar->instances(recurringIncidence);
        KCalCore::Incidence::Ptr exportableIncidence = IncidenceHandler::incidenceToExport(recurringIncidence, instances);

        // store the base recurring event into the in-memory calendar
        if (!memoryCalendar->addIncidence(exportableIncidence)) {
            LOG_WARNING("Unable to add base series event to in-memory calendar for incidence:"
                        << updatedIncidence->uid() << ":" << updatedIncidence->recurrenceId().toString());
            return QString();
        }

        // now create the persistent occurrences in the in-memory calendar
        Q_FOREACH (KCalCore::Incidence::Ptr instance, instances) {
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
    } else {
        KCalCore::Incidence::Ptr exportableIncidence = IncidenceHandler::incidenceToExport(updatedIncidence);
        memoryCalendar->addIncidence(exportableIncidence);
    }

    KCalCore::ICalFormat icalFormat;
    return icalFormat.toString(memoryCalendar, QString(), false);
}

bool NotebookSyncAgent::updateIncidence(KCalCore::Incidence::Ptr incidence,
                                        const QString &resourceHref,
                                        const QString &resourceEtag,
                                        bool isKnownOrphan,
                                        bool *criticalError)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (incidence.isNull()) {
        return false;
    }

    const QString &nbuid = QStringLiteral("NBUID:%1:%2").arg(mNotebook->uid()).arg(incidence->uid());

    // Load from storage any matching incidence by uid or modified uid.
    mStorage->load(incidence->uid(), incidence->hasRecurrenceId() ? incidence->recurrenceId() : KDateTime());
    mStorage->load(nbuid, incidence->hasRecurrenceId() ? incidence->recurrenceId() : KDateTime());

    KCalCore::Incidence::Ptr storedIncidence;
    switch (incidence->type()) {
    case KCalCore::IncidenceBase::TypeEvent:
        storedIncidence = mCalendar->event(incidence->uid(), incidence->hasRecurrenceId() ? incidence->recurrenceId() : KDateTime());
        if (!storedIncidence) {
            incidence->setUid(nbuid);
            storedIncidence = mCalendar->event(incidence->uid(), incidence->hasRecurrenceId() ? incidence->recurrenceId() : KDateTime());
        }
        break;
    case KCalCore::IncidenceBase::TypeTodo:
        storedIncidence = mCalendar->todo(incidence->uid());
        if (!storedIncidence) {
            incidence->setUid(nbuid);
            storedIncidence = mCalendar->todo(incidence->uid());
        }
        break;
    case KCalCore::IncidenceBase::TypeJournal:
        storedIncidence = mCalendar->journal(incidence->uid());
        if (!storedIncidence) {
            incidence->setUid(nbuid);
            storedIncidence = mCalendar->journal(incidence->uid());
        }
        break;
    case KCalCore::IncidenceBase::TypeFreeBusy:
    case KCalCore::IncidenceBase::TypeUnknown:
        qWarning() << "Unsupported incidence type:" << incidence->type();
        return false;
    }
    if (storedIncidence) {
        if (incidence->status() == KCalCore::Incidence::StatusCanceled
                || incidence->customStatus().compare(QStringLiteral("CANCELLED"), Qt::CaseInsensitive) == 0) {
            LOG_DEBUG("Queuing existing event for deletion:" << storedIncidence->uid() << storedIncidence->recurrenceId().toString()
                                                             << resourceHref << resourceEtag);
            mLocalDeletions.append(incidence);
        } else {
            LOG_DEBUG("Updating existing event:" << storedIncidence->uid() << storedIncidence->recurrenceId().toString()
                                                 << resourceHref << resourceEtag);
            storedIncidence->startUpdates();
            IncidenceHandler::prepareImportedIncidence(incidence);
            IncidenceHandler::copyIncidenceProperties(storedIncidence, incidence);

            // if this incidence is a recurring incidence, we should get all persistent occurrences
            // and add them back as EXDATEs.  This is because mkcal expects that dissociated
            // single instances will correspond to an EXDATE, but most sync servers do not (and
            // so will not include the RECURRENCE-ID values as EXDATEs of the parent).
            if (storedIncidence->recurs()) {
                KCalCore::Incidence::List instances = mCalendar->instances(incidence);
                Q_FOREACH (KCalCore::Incidence::Ptr instance, instances) {
                    if (instance->hasRecurrenceId()) {
                        storedIncidence->recurrence()->addExDateTime(instance->recurrenceId());
                    }
                }
            }

            // ensure we set the url and etag as required.
            updateIncidenceHrefEtag(storedIncidence, resourceHref, resourceEtag);
            storedIncidence->endUpdates();
        }
    } else {
        // the new incidence will be either a new persistent occurrence, or a new base-series (or new non-recurring).
        LOG_DEBUG("Have new incidence:" << incidence->uid() << incidence->recurrenceId().toString()
                                        << resourceHref << resourceEtag);

        incidence->setUid(nbuid);
        if (incidence->hasRecurrenceId()) {
            // no dissociated occurrence exists already (ie, it's not an update), so create a new one.
            // need to detach, and then copy the properties into the detached occurrence.
            KCalCore::Incidence::Ptr recurringIncidence = mCalendar->event(incidence->uid(), KDateTime());
            if (!recurringIncidence) {
                recurringIncidence = mCalendar->event(nbuid, KDateTime());
            }
            if (isKnownOrphan) {
                // construct a parent series for this orphan, or update the previously constructed one.
                if (recurringIncidence.isNull()) {
                    // construct a recurring parent series for this orphan.
                    KCalCore::Incidence::Ptr parentSeries = KCalCore::Incidence::Ptr(new KCalCore::Event);
                    IncidenceHandler::prepareImportedIncidence(incidence);
                    IncidenceHandler::copyIncidenceProperties(parentSeries, incidence);
                    parentSeries->setRecurrenceId(KDateTime());
                    parentSeries->recurrence()->addRDateTime(incidence->recurrenceId());
                    parentSeries->setUid(nbuid);
                    setIncidenceHrefUri(parentSeries, resourceHref);
                    setIncidenceETag(parentSeries, resourceEtag);
                    LOG_DEBUG("creating parent series for orphan exception event:" << incidence->uid() << incidence->recurrenceId().toString());
                    bool parentAdded = mCalendar->addEvent(parentSeries.staticCast<KCalCore::Event>(), mNotebook->uid());
                    if (!parentAdded) {
                        LOG_WARNING("error: could not create parent series for orphan exception event:" << incidence->uid() << incidence->recurrenceId().toString());
                    } else {
                        mStorage->load(nbuid, KDateTime());
                        recurringIncidence = mCalendar->event(nbuid);
                    }
                } else if (!recurringIncidence.isNull()) {
                    // the parent was already added for this orphan (e.g. for a separate orphan exception occurrence)
                    // but we still need to make sure that the rdate exists for this one.
                    if (!recurringIncidence->recurrence()->rDateTimes().contains(incidence->recurrenceId())) {
                        LOG_DEBUG("adding rdate to parent series for orphan exception event:" << incidence->uid() << incidence->recurrenceId().toString());
                        recurringIncidence->startUpdates();
                        recurringIncidence->recurrence()->addRDateTime(incidence->recurrenceId());
                        recurringIncidence->endUpdates();
                    }
                }
            }
            // now we should have a parent series from which we can dissociate the exception occurrence.
            if (!recurringIncidence.isNull()) {
                KCalCore::Incidence::Ptr occurrence = setExceptionIncidence(mCalendar, recurringIncidence, incidence);
                if (occurrence.isNull()) {
                    LOG_WARNING("error: could not dissociate occurrence from recurring event:" << incidence->uid() << incidence->recurrenceId().toString());
                    return false;
                }

                setIncidenceHrefUri(occurrence, resourceHref);
                setIncidenceETag(occurrence, resourceEtag);
                if (!mCalendar->addEvent(occurrence.staticCast<KCalCore::Event>(), mNotebook->uid())) {
                    LOG_WARNING("error: could not add dissociated occurrence to calendar");
                    return false;
                }
                LOG_DEBUG("Added new occurrence incidence:" << occurrence->uid() << occurrence->recurrenceId().toString());
                return true;
            } else {
                LOG_WARNING("error: parent recurring incidence could not be retrieved:" << incidence->uid());
                return false;
            }
        }

        // just a new event without needing detach.
        IncidenceHandler::prepareImportedIncidence(incidence);
        setIncidenceHrefUri(incidence, resourceHref);
        setIncidenceETag(incidence, resourceEtag);

        bool added = false;
        switch (incidence->type()) {
        case KCalCore::IncidenceBase::TypeEvent:
            added = mCalendar->addEvent(incidence.staticCast<KCalCore::Event>(), mNotebook->uid());
            break;
        case KCalCore::IncidenceBase::TypeTodo:
            added = mCalendar->addTodo(incidence.staticCast<KCalCore::Todo>(), mNotebook->uid());
            break;
        case KCalCore::IncidenceBase::TypeJournal:
            added = mCalendar->addJournal(incidence.staticCast<KCalCore::Journal>(), mNotebook->uid());
            break;
        case KCalCore::IncidenceBase::TypeFreeBusy:
        case KCalCore::IncidenceBase::TypeUnknown:
            LOG_WARNING("Unsupported incidence type:" << incidence->type());
            return false;
        }
        if (added) {
            LOG_DEBUG("Added new incidence:" << incidence->uid() << incidence->recurrenceId().toString());
        } else {
            LOG_WARNING("Unable to add incidence" << incidence->uid() << incidence->recurrenceId().toString() << "to notebook" << mNotebook->uid());
            *criticalError = true;
            return false;
        }
    }
    return true;
}

bool NotebookSyncAgent::updateIncidences(const QList<Reader::CalendarResource> &resources)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // We need to coalesce any resources which have the same UID.
    // This can be the case if there is addition of both a recurring event,
    // and a modified occurrence of that event, in the same sync cycle.
    // To ensure that we deal with the original recurring event first,
    // we find the resource which includes that change and promote it
    // in the list (so that we deal with it before the other).
    QList<Reader::CalendarResource> orderedResources;
    for (int i = resources.count() - 1; i >= 0; --i) {
        bool prependedResource = false;
        for (int j = 0; j < resources[i].incidences.count(); ++j) {
            if (!resources[i].incidences[j]->hasRecurrenceId()) {
                // we have a non-occurrence event which needs promotion.
                orderedResources.prepend(resources[i]);
                prependedResource = true;
                break;
            }
        }
        if (!prependedResource) {
            // this resource needs to be appended.
            orderedResources.append(resources[i]);
        }
    }

    for (int i = 0; i < orderedResources.count(); ++i) {
        const Reader::CalendarResource &resource = orderedResources.at(i);
        if (!resource.incidences.size()) {
            continue;
        }

        // Each resource is either a single event series (or non-recurring event) OR
        // a list of updated/added persistent exceptions to an existing series.
        // If the resource contains an event series which includes the base incidence,
        // then we need to compare the local series with the remote series, to ensure
        // we remove any incidences which occur locally but not remotely.
        // However, if the resource's incidence list does not contain the base incidence,
        // but instead contains just persistent exceptions (ie, have recurrenceId) then
        // we can assume that no persistent exceptions were removed - only added/updated.
        QString uid = resource.incidences.first()->uid();
        Q_FOREACH (KCalCore::Incidence::Ptr incidence, resource.incidences) {
            if (incidence->uid() != uid) {
                LOG_WARNING("Updated incidence list contains incidences with non-matching uids!");
                return false; // this is always an error.  each resource corresponds to a single event series.
            }
        }

        // find the recurring incidence (parent) in the update list, and save it.
        // alternatively, it may be a non-recurring base incidence.
        bool criticalError = false;
        int parentIndex = -1;
        for (int i = 0; i < resource.incidences.size(); ++i) {
            if (!resource.incidences[i]->hasRecurrenceId()) {
                parentIndex = i;
                break;
            }
        }

        if (parentIndex == -1) {
            LOG_DEBUG("No parent or base incidence in resource's incidence list, performing direct updates to" << resource.incidences.size() << "persistent occurrences");
            for (int i = 0; i < resource.incidences.size(); ++i) {
                KCalCore::Incidence::Ptr remoteInstance = resource.incidences[i];
                LOG_DEBUG("Updating orphan persistent occurrence:" << remoteInstance->uid() << remoteInstance->summary());
                updateIncidence(remoteInstance, resource.href, resource.etag, true, &criticalError);
                if (criticalError) {
                    LOG_WARNING("Error saving updated persistent occurrence of resource" << resource.href << ":" << remoteInstance->recurrenceId().toString());
                    return false;
                }
            }
            continue; // finished with this resource, now handle the next one
        }

        // if there was a parent / base incidence, then we need to compare local/remote lists.
        // load the local (persistent) occurrences of the series.  Later we will update or remove them as required.
        KCalCore::Incidence::Ptr localBaseIncidence = mCalendar->incidence(uid, KDateTime());
        KCalCore::Incidence::List localInstances;
        if (!localBaseIncidence.isNull() && localBaseIncidence->recurs()) {
            localInstances = mCalendar->instances(localBaseIncidence); // TODO: should we use the updatedBaseIncidence here instead?
        }

        // first save the added/updated base incidence
        LOG_DEBUG("Saving the added/updated base incidence before saving persistent exceptions:" << resource.incidences[parentIndex]->uid());
        KCalCore::Incidence::Ptr updatedBaseIncidence = resource.incidences[parentIndex];
        updateIncidence(updatedBaseIncidence, resource.href, resource.etag, false, &criticalError); // update the base incidence first.
        if (criticalError) {
            LOG_WARNING("Error saving base incidence of resource" << resource.href);
            return false;
        }

        // update persistent exceptions which are in the remote list.
        QList<KDateTime> remoteRecurrenceIds;
        for (int i = 0; i < resource.incidences.size(); ++i) {
            if (i == parentIndex) {
                continue; // already handled this one.
            }

            LOG_DEBUG("Now saving a persistent exception:" << resource.incidences[i]->recurrenceId().toString());
            KCalCore::Incidence::Ptr remoteInstance = resource.incidences[i];
            remoteRecurrenceIds.append(remoteInstance->recurrenceId());
            updateIncidence(remoteInstance, resource.href, resource.etag, false, &criticalError);
            if (criticalError) {
                LOG_WARNING("Error saving updated persistent occurrence of resource" << resource.href << ":" << remoteInstance->recurrenceId().toString());
                return false;
            }
        }

        // remove persistent exceptions which are not in the remote list.
        for (int i = 0; i < localInstances.size(); ++i) {
            KCalCore::Incidence::Ptr localInstance = localInstances[i];
            if (!remoteRecurrenceIds.contains(localInstance->recurrenceId())) {
                LOG_DEBUG("Now removing remotely-removed persistent occurrence:" << localInstance->recurrenceId().toString());
                if (!mCalendar->deleteIncidence(localInstance)) {
                    LOG_WARNING("Error removing remotely deleted persistent occurrence of resource" << resource.href << ":" << localInstance->recurrenceId().toString());
                    return false;
                }
            }
        }
    }

    return true;
}

bool NotebookSyncAgent::deleteIncidences(KCalCore::Incidence::List deletedIncidences)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;
    Q_FOREACH (KCalCore::Incidence::Ptr doomed, deletedIncidences) {
        mStorage->load(doomed->uid());
        if (!mCalendar->deleteIncidence(mCalendar->incidence(doomed->uid(), doomed->recurrenceId()))) {
            LOG_WARNING("Unable to delete incidence: " << doomed->uid() << doomed->recurrenceId().toString());
            return false;
        } else {
            LOG_DEBUG("Deleted incidence: " << doomed->uid() << doomed->recurrenceId().toString());
        }
    }
    return true;
}

void NotebookSyncAgent::updateHrefETag(const QString &uid, const QString &href, const QString &etag) const
{
    KCalCore::Incidence::Ptr localBaseIncidence = mCalendar->incidence(uid);
    if (localBaseIncidence) {
        localBaseIncidence->startUpdates();
        updateIncidenceHrefEtag(localBaseIncidence, href, etag);
        localBaseIncidence->endUpdates();
        if (localBaseIncidence->recurs()) {
            Q_FOREACH (KCalCore::Incidence::Ptr instance,
                       mCalendar->instances(localBaseIncidence)) {
                instance->startUpdates();
                updateIncidenceHrefEtag(instance, href, etag);
                instance->endUpdates();
            }
        }
    } else {
        LOG_WARNING("Unable to find base incidence: " << uid);
    }
}
