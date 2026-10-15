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

#include "logging.h"
#include <SyncResults.h>

#include <KCalendarCore/Incidence>

#include <KCalendarCore/MemoryCalendar>

#include <QDebug>

#define NOTEBOOK_FUNCTION_CALL_TRACE qCDebug(lcCalDavTrace) << Q_FUNC_INFO << mRemoteCalendarPath

namespace {
    QString createIncidenceHrefUri(KCalendarCore::Incidence::Ptr incidence, const QString &remoteCalendarPath)
    {
        if (incidence->uid().startsWith(QString::fromLatin1("NBUID:"))) {
            const QString uid = incidence->uid();
            return remoteCalendarPath + uid.mid(uid.indexOf(':', 6)+1) + ".ics"; // remove NBUID:NotebookUid:
        } else {
            // In case UID is coming from an importation it can be
            // whatever. A proper sanitation woud be to use QUrl::toPercentEncoding()
            // but the percent encoded '/' character is creating issue with
            // some server in multiget requests.
            return remoteCalendarPath + incidence->uid().replace('/', '-') + ".ics";
        }
    }

    bool incidenceWithin(KCalendarCore::Incidence::Ptr incidence,
                         const QDateTime &from, const QDateTime &to)
    {
        return incidence->dtStart() <= to
            && (!incidence->recurs()
                || !incidence->recurrence()->endDateTime().isValid()
                || incidence->recurrence()->endDateTime() >= from)
            && (incidence->recurs()
                || incidence->dateTime(KCalendarCore::Incidence::RoleDisplayEnd) >= from);
    }

    typedef enum {
          REMOTE,
          LOCAL
    } Target;
    void summarizeResults(Buteo::TargetResults *results, Target target,
                          Buteo::TargetResults::ItemOperation operation,
                          const QHash<QString, QByteArray> &failingHrefs,
                          const KCalendarCore::Incidence::List &incidences)
    {
        for (int i = 0; i < incidences.size(); i++) {
            const QString href = IncidenceHandler::uri(incidences[i]);
            const QString uid(incidences[i]->instanceIdentifier());
            const QHash<QString, QByteArray>::ConstIterator failure = failingHrefs.find(href);
            const Buteo::TargetResults::ItemOperationStatus status = failure != failingHrefs.constEnd()
                ? Buteo::TargetResults::ITEM_OPERATION_FAILED
                : Buteo::TargetResults::ITEM_OPERATION_SUCCEEDED;
            const QString data = failure != failingHrefs.constEnd() ? QString::fromUtf8(failure.value()) : QString();
            if (target == LOCAL) {
                results->addLocalDetails(uid, operation, status, data);
            } else {
                results->addRemoteDetails(uid, operation, status, data);
            }
        }
    }
}

static const QByteArray PATH_PROPERTY = QByteArrayLiteral("remoteCalendarPath");
static const QByteArray EMAIL_PROPERTY = QByteArrayLiteral("userPrincipalEmail");
static const QByteArray SERVER_COLOR_PROPERTY = QByteArrayLiteral("serverColor");

NotebookSyncAgent::NotebookSyncAgent(int accountId,
                                     Buteo::Dav::Client *davClient,
                                     const QString &remotePath,
                                     QObject *parent)
    : QObject(parent)
    , mDAV(davClient)
    , mRemoteCalendarPath(remotePath)
    , mSyncMode(NoSyncMode)
    , mRetriedReport(false)
    , mNotebookNeedsDeletion(false)
    , mEnableUpsync(true)
    , mEnableDownsync(true)
    , mReadOnlyFlag(false)
{
    // Yahoo! seems to double-percent-encode for some reason
    if (mDAV->serverAddress().contains(QStringLiteral("caldav.calendar.yahoo.com"))) {
        mRemoteCalendarPath = QUrl::fromPercentEncoding(mRemoteCalendarPath.toUtf8());
    }

    mLocalStorage = new LocalStorage(accountId, mRemoteCalendarPath);
}

NotebookSyncAgent::~NotebookSyncAgent()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    delete mLocalStorage;
}

void NotebookSyncAgent::abort()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    disconnect(mDAV, 0, this, 0);

    emit finished();
}

void NotebookSyncAgent::setNotebookFromInfo(const Buteo::Dav::CalendarInfo &info,
                                            const QString &userEmail,
                                            const QString &pluginName,
                                            const QString &syncProfile)
{
    mLocalStorage->setNotebookMetadata(info, userEmail, pluginName, syncProfile);
    mReadOnlyFlag = (info.privileges & Buteo::Dav::READ) && !(info.privileges & Buteo::Dav::WRITE);
    mNotebookName = info.displayName.toHtmlEscaped();
}

void NotebookSyncAgent::startSync(const QDateTime &fromDateTime,
                                  const QDateTime &toDateTime,
                                  bool withUpsync, bool withDownsync)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    disconnect(mDAV, 0, this, 0);
    connect(mDAV, &Buteo::Dav::Client::calendarEtagsFinished, this, &NotebookSyncAgent::processETags);
    connect(mDAV, &Buteo::Dav::Client::calendarResourcesFinished, this, &NotebookSyncAgent::reportRequestFinished);
    connect(mDAV, &Buteo::Dav::Client::sendCalendarFinished, this, &NotebookSyncAgent::resourceSent);
    connect(mDAV, &Buteo::Dav::Client::deleteFinished, this, &NotebookSyncAgent::resourceDeleted);

    mFromDateTime = fromDateTime;
    mToDateTime = toDateTime;
    mEnableUpsync = withUpsync;
    mEnableDownsync = withDownsync;
    mPendingActions = 0;
    if (mLocalStorage->lastSyncDate().isNull()) {
/*
    Slow sync mode:

    1) Get all calendars on the server using Report::getAllEvents()
    2) Save all received calendar data to disk.

    Step 2) is triggered by CalDavClient once *all* notebook syncs have finished.
 */
        qCDebug(lcCalDav) << "Start slow sync for remote path:" << mRemoteCalendarPath
                          << "between" << fromDateTime << "to" << toDateTime;
        mSyncMode = SlowSync;
        mEnableDownsync = true;

        // Even if down sync is disabled in profile, we down sync the
        // remote calendar the first time, by design.
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
        qCDebug(lcCalDav) << "Start quick sync for remote path:" << mRemoteCalendarPath
                  << "between" << fromDateTime << "to" << toDateTime
                  << ", sync changes since" << mLocalStorage->lastSyncDate();
        mSyncMode = QuickSync;

        fetchRemoteChanges();
    }
}

void NotebookSyncAgent::sendReportRequest(const QStringList &remoteUris)
{
    // must be m_syncMode = SlowSync.
    mPendingActions += 1;
    if (remoteUris.isEmpty()) {
        mDAV->getCalendarResources(mRemoteCalendarPath, mFromDateTime, mToDateTime);
    } else {
        mDAV->getCalendarResources(mRemoteCalendarPath, remoteUris);
    }
}

void NotebookSyncAgent::fetchRemoteChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // must be m_syncMode = QuickSync.
    mPendingActions += 1;
    mDAV->getCalendarEtags(mRemoteCalendarPath, mFromDateTime, mToDateTime);
}

void NotebookSyncAgent::reportRequestFinished(const Buteo::Dav::Client::Reply &reply,
                                              const QList<Buteo::Dav::Resource> &resources)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (reply.uri != mRemoteCalendarPath)
        return;

    qCDebug(lcCalDav) << "report request finished with result:" << reply.hasError() << reply.errorMessage;

    if (!reply.hasError()) {
        // NOTE: we don't store the remote artifacts yet
        // Instead, we just emit finished (for this notebook)
        // Once ALL notebooks are finished, then we apply the remote changes.
        // This prevents the worst partial-sync issues.
        for (const Buteo::Dav::Resource &resource : resources) {
            if (!resource.data.isEmpty()) {
                mReceivedCalendarResources.append(resource);
                if (mSentUids.contains(resource.href) && resource.etag.isEmpty()) {
                    // Asked for a resource etag but didn't get it.
                    mFailingUploads.insert(resource.href, QByteArray("Unable to retrieve etag."));
                }
            }
        }
        qCDebug(lcCalDav) << "Report request finished: received:"
                  << resources.length() << "iCal blobs";
    } else if (mSyncMode == SlowSync
               && reply.networkError == QNetworkReply::AuthenticationRequiredError
               && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        qCWarning(lcCalDav) << "Retrying REPORT after request failed with QNetworkReply::AuthenticationRequiredError";
        mRetriedReport = true;
        sendReportRequest();
    } else if (mSyncMode == SlowSync
               && reply.networkError == QNetworkReply::ContentNotFoundError) {
        // The remote calendar resource was removed after we created the account but before first sync.
        // We don't perform resource discovery in CalDAV during each sync cycle,
        // so we can have local calendar metadata for remotely removed calendars.
        // In this case, we just skip sync of this calendar, as it was deleted.
        mNotebookNeedsDeletion = true;
        qCDebug(lcCalDav) << "calendar" << reply.uri << "was deleted remotely, skipping sync locally.";
    } else if (mSyncMode == SlowSync) {
        setFatal(reply.uri, reply.errorData);
        return;
    } else {
        for (const QString &href : mSentUids.keys()) {
            mFailingUpdates.insert(href, reply.errorData);
        }
    }

    mSentUids.clear();
    requestFinished();
}

void NotebookSyncAgent::processETags(const Buteo::Dav::Client::Reply &reply, const QHash<QString, QString> &etags)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (reply.uri != mRemoteCalendarPath)
        return;

    qCDebug(lcCalDav) << "fetch etags finished with result:" << reply.hasError() << reply.errorMessage;

    if (!reply.hasError()) {
        qCDebug(lcCalDav) << "Process tags for server path" << reply.uri;
        // calculate the local and remote delta.
        if (!calculateDelta(etags,
                            &mLocalAdditions,
                            &mLocalModifications,
                            &mLocalDeletions,
                            &mRemoteChanges,
                            &mRemoteDeletions)) {
            qCWarning(lcCalDav) << "unable to calculate the sync delta for:" << mRemoteCalendarPath;
            setFatal(reply.uri, "Unable to calculate the sync delta.");
            return;
        }

        if (mEnableDownsync && !mRemoteChanges.isEmpty()) {
            // some incidences have changed on the server, so fetch the new details
            sendReportRequest(mRemoteChanges.toList());
        }
        sendLocalChanges();
    } else if (reply.networkError == QNetworkReply::AuthenticationRequiredError && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        qCWarning(lcCalDav) << "Retrying ETAG REPORT after request failed with QNetworkReply::AuthenticationRequiredError";
        mRetriedReport = true;
        fetchRemoteChanges();
    } else if (reply.networkError == QNetworkReply::ContentNotFoundError) {
        // The remote calendar resource was removed.
        // We don't perform resource discovery in CalDAV during each sync cycle,
        // so we can have local calendars which mirror remotely-removed calendars.
        // In this situation, we need to delete the local calendar.
        mNotebookNeedsDeletion = true;
        qCDebug(lcCalDav) << "calendar" << reply.uri << "was deleted remotely, marking for deletion locally.";
    } else {
        setFatal(reply.uri, reply.errorData);
        return;
    }

    requestFinished();
}

void NotebookSyncAgent::sendLocalChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    mFailingUploads.clear();

    if (!mLocalAdditions.count() && !mLocalModifications.count() && !mLocalDeletions.count()) {
        // no local changes to upsync.
        // we're finished syncing.
        qCDebug(lcCalDav) << "no local changes to upsync - finished with notebook"
                          << mRemoteCalendarPath;
        return;
    } else if (!mEnableUpsync) {
        qCDebug(lcCalDav) << "Not upsyncing local changes, upsync disable in profile.";
        return;
    } else if (mReadOnlyFlag) {
        qCDebug(lcCalDav) << "Not upsyncing local changes, upstream read only calendar.";
        return;
    } else {
        qCDebug(lcCalDav) << "upsyncing local changes: A/M/R:" << mLocalAdditions.count()
                          << "/" << mLocalModifications.count() << "/" << mLocalDeletions.count();
    }

    // For deletions, if a persistent exception is deleted we may need to do a PUT
    // containing all of the still-existing events in the series.
    QSet<QString> uris = mLocalStorage->toDeletedResources(mLocalDeletions, &mLocalModifications);
    for (const QString &uri : uris) {
        qCDebug(lcCalDav) << "deleting whole series:" << uri;
        mDAV->deleteResource(uri);
        mPendingActions += 1;
    }
    // Incidence will be actually purged only if all operations succeed.
    mPurgeList += mLocalDeletions;

    mSentUids.clear();
    KCalendarCore::Incidence::List toUpload(mLocalAdditions + mLocalModifications);
    for (int i = 0; i < toUpload.count(); i++) {
        const QString href = IncidenceHandler::uri(toUpload[i]);
        if (mSentUids.contains(href)) {
            qCDebug(lcCalDav) << "Already handled upload" << i << "via series update";
            continue; // already handled this one, as a result of a previous update of another occurrence in the series.
        }
        Buteo::Dav::Resource res = mLocalStorage->toResource(toUpload[i]);
        if (res.status.isEmpty()) {
            qCDebug(lcCalDav) << "Uploading incidence" << i << "via PUT for uid:" << toUpload[i]->uid();
            mDAV->sendCalendarResource(res.href, res.data, res.etag);
            mPendingActions += 1;
            mSentUids.insert(href, toUpload[i]->uid());
        } else {
            mFailingUploads.insert(href, res.status.toUtf8());
        }
    }
}

void NotebookSyncAgent::resourceSent(const Buteo::Dav::Client::Reply &reply, const QString &etag)
{
    if (mSentUids.contains(reply.uri)) {
        if (reply.hasError()) {
            mFailingUploads.insert(reply.uri, reply.errorData);
            // Don't try to get etag later for a failed upload.
            mSentUids.remove(reply.uri);
        } else if (!etag.isEmpty()) {
            // Apply Etag and Href changes immediately since incidences are now
            // for sure on server.
            mLocalStorage->resourceIsSynced(reply.uri, etag, mSentUids.take(reply.uri));
        }

        requestFinished();
    }
}

void NotebookSyncAgent::resourceDeleted(const Buteo::Dav::Client::Reply &reply)
{
    if (reply.uri.startsWith(mRemoteCalendarPath)) {
        if (reply.hasError()) {
            mFailingUploads.insert(reply.uri, reply.errorData);
            // Don't purge yet the locally deleted incidence.
            KCalendarCore::Incidence::List::Iterator it = mPurgeList.begin();
            while (it != mPurgeList.end()) {
                if (IncidenceHandler::uri(*it) == reply.uri) {
                    it = mPurgeList.erase(it);
                } else {
                    ++it;
                }
            }
        }

        requestFinished();
    }
}

void NotebookSyncAgent::markNotebookAsDeleted()
{
    qCDebug(lcCalDav) << "Flag notebook for deletion:" << mRemoteCalendarPath;
    mNotebookNeedsDeletion = true;
    mEnableDownsync = true;
    mPendingActions = 0;
    emit finished();
}

bool NotebookSyncAgent::applyRemoteChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (mEnableDownsync && mNotebookNeedsDeletion) {
        if (!mLocalStorage->deleteNotebook()) {
            qCWarning(lcCalDav) << "Cannot delete notebook from storage.";
            return false;
        }
        return true;
    }

    if (!mLocalStorage->ensureNotebook()) {
        qCWarning(lcCalDav) << "Cannot ensure notebook in storage.";
        return false;
    }
    bool success = true;
    if (mEnableDownsync) {
        success = updateIncidences(mReceivedCalendarResources) && success;
        success = deleteIncidences(mRemoteDeletions) && success;
    }
    success = mLocalStorage->save() && success;
    // Silently ignore failed purge action in database.
    mLocalStorage->purgeDeleted(mPurgeList);

    return success;
}

Buteo::TargetResults NotebookSyncAgent::result() const
{
    if (mSyncMode == SlowSync) {
        return Buteo::TargetResults(mNotebookName,
                                    Buteo::ItemCounts(mRemoteAdditions.count(), 0, 0),
                                    Buteo::ItemCounts());
    } else {
        Buteo::TargetResults results(mNotebookName);

        summarizeResults(&results, LOCAL, Buteo::TargetResults::ITEM_ADDED,
                         mFailingUpdates, mRemoteAdditions);
        summarizeResults(&results, LOCAL, Buteo::TargetResults::ITEM_DELETED,
                         mFailingUpdates, mRemoteDeletions);
        summarizeResults(&results, LOCAL, Buteo::TargetResults::ITEM_MODIFIED,
                         mFailingUpdates, mRemoteModifications);
        summarizeResults(&results, REMOTE, Buteo::TargetResults::ITEM_ADDED,
                         mFailingUploads, mLocalAdditions);
        summarizeResults(&results, REMOTE, Buteo::TargetResults::ITEM_DELETED,
                         mFailingUploads, mLocalDeletions);
        summarizeResults(&results, REMOTE, Buteo::TargetResults::ITEM_MODIFIED,
                         mFailingUploads, mLocalModifications);

        return results;
    }
}

void NotebookSyncAgent::requestFinished()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    mPendingActions -= 1;

    if (!mPendingActions && !mSentUids.isEmpty()) {
        // Request for etags.
        sendReportRequest(mSentUids.keys());
    }

    if (!mPendingActions) {
        // Flag (or remove flag) for all failing (or not) local changes.
        QSet<QString> failures;
        for (KCalendarCore::Incidence::Ptr incidence : mLocalAdditions + mLocalModifications) {
            if (mFailingUploads.contains(IncidenceHandler::uri(incidence)))
                failures.insert(incidence->uid());
        }
        mLocalStorage->flagUploadFailures(failures.toList());

        emit finished();
    }
}

void NotebookSyncAgent::setFatal(const QString &uri, const QByteArray &errorData)
{
    mFailingUpdates.insert(uri, errorData);
    mFatalUri = uri;
    abort();
}

bool NotebookSyncAgent::isFinished() const
{
    return !mPendingActions;
}

bool NotebookSyncAgent::isCompleted() const
{
    return mFatalUri.isEmpty();
}

bool NotebookSyncAgent::isDeleted() const
{
    return (mEnableDownsync && mNotebookNeedsDeletion);
}

bool NotebookSyncAgent::hasDownloadErrors() const
{
    return !mFailingUpdates.isEmpty();
}

bool NotebookSyncAgent::hasUploadErrors() const
{
    return !mFailingUploads.isEmpty();
}

const QString& NotebookSyncAgent::path() const
{
    return mRemoteCalendarPath;
}

// ------------------------------ Utility / implementation functions.

// called in the QuickSync codepath after fetching etags for remote resources.
// from the etags, we can determine the local and remote sync delta.
bool NotebookSyncAgent::calculateDelta(
        // in parameters:
        const QHash<QString, QString> &remoteUriEtags, //  remoteEtags: map of uri to etag which exist on the remote server.
        // out parameters:
        KCalendarCore::Incidence::List *localAdditions,
        KCalendarCore::Incidence::List *localModifications,
        KCalendarCore::Incidence::List *localDeletions,
        QSet<QString> *remoteChanges,
        KCalendarCore::Incidence::List *remoteDeletions)
{
    KCalendarCore::Incidence::List localIncidences, deletedIncidences;
    QSet<QString> modifiedInstances;
    if (!mLocalStorage->snapshot(&localIncidences, &modifiedInstances, &deletedIncidences)) {
        qCWarning(lcCalDav) << "aborting sync for path:" << mRemoteCalendarPath;
        return false;
    }
    
    // separate them into buckets.
    // note that each remote URI can be associated with multiple local incidences (due recurrenceId incidences)
    // Here we can determine local additions, modifications and remote modifications, deletions.
    QSet<QString> localUris;
    for (KCalendarCore::Incidence::Ptr incidence : const_cast<const KCalendarCore::Incidence::List&>(localIncidences)) {
        bool modified = modifiedInstances.contains(incidence->instanceIdentifier());
        QString remoteUri = IncidenceHandler::uri(incidence);
        if (remoteUri.isEmpty()) {
            remoteUri = createIncidenceHrefUri(incidence, mRemoteCalendarPath);
            // Imported exceptions don't have URI and etag inherited from parent.
            if (!incidence->hasRecurrenceId() && remoteUriEtags.contains(remoteUri)) {
                // we previously upsynced this incidence but then connectivity died and etag was not set.
                if (!modified) {
                    qCDebug(lcCalDav) << "have previously partially upsynced local addition, needs uri update:" << remoteUri;
                    // Treat it as a remote modification and trigger download for etag and uri update.
                    mPulledUids.insert(remoteUri, incidence->uid());
                    remoteChanges->insert(remoteUri);
                } else {
                    qCDebug(lcCalDav) << "have local modification to partially synced incidence:" << incidence->uid()
                                      << incidence->recurrenceId();
                    // note: we cannot check the etag to determine if it changed also on server side.
                    // we assume here local modifications only.
                    IncidenceHandler::setUri(incidence, remoteUri);
                    IncidenceHandler::setEtag(incidence, remoteUriEtags.value(remoteUri));
                    localModifications->append(incidence);
                }
            } else if (IncidenceHandler::uploadIsAllowed(incidence)) {
                // it doesn't exist on remote side... new local addition.
                qCDebug(lcCalDav) << "have new local addition:" << incidence->uid() << incidence->recurrenceId();
                IncidenceHandler::setUri(incidence, remoteUri);
                localAdditions->append(incidence);
            }
        } else {
            // this is a previously-synced incidence with a remote uri,
            // OR a newly-added persistent occurrence to a previously-synced recurring series.
            if (!remoteUriEtags.contains(remoteUri)) {
                if (!incidenceWithin(incidence, mFromDateTime, mToDateTime)) {
                    qCDebug(lcCalDav) << "ignoring out-of-range missing remote incidence:" << incidence->uid()
                                      << incidence->recurrenceId();
                } else if (IncidenceHandler::deleteIsAllowed(incidence)) {
                    qCDebug(lcCalDav) << "have remote deletion of previously synced incidence:" << incidence->uid()
                                      << incidence->recurrenceId();
                    // Ignoring local modifications if any.
                    remoteDeletions->append(incidence);
                } else if (IncidenceHandler::resetDeleteFailure(incidence)) {
                    qCDebug(lcCalDav) << "reset remote deletion:" << incidence->uid() << incidence->recurrenceId().toString();
                    localAdditions->append(incidence);
                }
            } else if (IncidenceHandler::isLocalException(incidence)) {
                if (IncidenceHandler::etag(incidence) == remoteUriEtags.value(remoteUri)) {
                    qCDebug(lcCalDav) << "Found new locally-added persistent exception:" << incidence->uid()
                                      << incidence->recurrenceId().toString() << ":" << remoteUri;
                    localAdditions->append(incidence);
                } else {
                    qCDebug(lcCalDav) << "ignoring new locally-added persistent exception to remotely modified incidence:"
                                      << incidence->uid() << incidence->recurrenceId().toString() << ":" << remoteUri;
                    mPulledUids.insert(remoteUri, incidence->uid());
                    remoteChanges->insert(remoteUri);
                }
            } else if (IncidenceHandler::etag(incidence) != remoteUriEtags.value(remoteUri)) {
                qCDebug(lcCalDav) << "have remote modification to previously synced incidence at:" << remoteUri;
                if (IncidenceHandler::updateIsAllowed(incidence)) {
                    qCDebug(lcCalDav) << "device etag:" << IncidenceHandler::etag(incidence)
                                      << "server etag:" << remoteUriEtags.value(remoteUri);
                    mPulledUids.insert(remoteUri, incidence->uid());
                    // Ignoring local modifications if any.
                    remoteChanges->insert(remoteUri);
                } else if (IncidenceHandler::resetUpdateFailure(incidence)) {
                    qCDebug(lcCalDav) << "reset remote modification:" << incidence->uid()
                                      << incidence->recurrenceId().toString();
                    IncidenceHandler::setEtag(incidence, remoteUriEtags.value(remoteUri));
                    localModifications->append(incidence);
                } else {
                    qCDebug(lcCalDav) << "ignoring remote modification of flagged incidence:"
                                      << incidence->instanceIdentifier();
                }
            } else if (modified) {
                // this is a real local modification.
                qCDebug(lcCalDav) << "have local modification:" << incidence->uid() << incidence->recurrenceId().toString();
                localModifications->append(incidence);
            } else if (IncidenceHandler::uploadIsAllowed(incidence)) {
                // this one failed to upload last time, we retry it.
                qCDebug(lcCalDav) << "have failing to upload incidence:" << incidence->uid()
                                  << incidence->recurrenceId().toString();
                localModifications->append(incidence);
            } else if (IncidenceHandler::resetUploadFailure(incidence)) {
                // scratch previously failing upload with server version.
                qCDebug(lcCalDav) << "reset failing to upload incidence:" << incidence->uid()
                                  << incidence->recurrenceId().toString();
                mPulledUids.insert(remoteUri, incidence->uid());
                remoteChanges->insert(remoteUri);
            }
        }
        localUris.insert(remoteUri);
    }

    // List all local deletions reported by mkcal.
    for (KCalendarCore::Incidence::Ptr incidence : const_cast<const KCalendarCore::Incidence::List&>(deletedIncidences)) {
        QString remoteUri = IncidenceHandler::uri(incidence);
        if (remoteUri.isEmpty()) {
            remoteUri = createIncidenceHrefUri(incidence, mRemoteCalendarPath);
            if (remoteUriEtags.contains(remoteUri)) {
                // we originally upsynced this pure-local addition, but then connectivity was
                // lost before we updated the uid of it locally to include the remote uri.
                // subsequently, the user deleted the incidence.
                // Hence, it exists remotely, and has been deleted locally.
                qCDebug(lcCalDav) << "have local deletion for partially synced incidence:"
                                  << incidence->uid() << incidence->recurrenceId().toString();
                // We will treat this as a local deletion.
                IncidenceHandler::setUri(incidence, remoteUri);
                IncidenceHandler::setEtag(incidence, remoteUriEtags.value(remoteUri));
            }
        }
        if (remoteUriEtags.contains(remoteUri)) {
            if (IncidenceHandler::etag(incidence) == remoteUriEtags.value(remoteUri)) {
                // the incidence was previously synced successfully.  it has now been deleted locally.
                qCDebug(lcCalDav) << "have local deletion for previously synced incidence:"
                                  << incidence->uid() << incidence->recurrenceId().toString();
                localDeletions->append(incidence);
            } else {
                // Sub-optimal case for persistent exceptions.
                // TODO: improve handling of this case.
                qCDebug(lcCalDav) << "ignoring local deletion due to remote modification:"
                                  << incidence->uid() << incidence->recurrenceId().toString();
                mPurgeList.append(incidence);
                remoteChanges->insert(remoteUri);
            }
            localUris.insert(remoteUri);
        } else {
            // it was either already deleted remotely, or was never upsynced from the local prior to deletion.
            qCDebug(lcCalDav) << "ignoring local deletion of non-existent remote incidence:"
                              << incidence->uid() << incidence->recurrenceId().toString() << "at" << remoteUri;
            mPurgeList.append(incidence);
        }
    }

    // now determine remote additions.
    const int nRemoteModifications = remoteChanges->size();
    for (const QString &remoteUri : remoteUriEtags.keys()) {
        if (!localUris.contains(remoteUri)) {
            qCDebug(lcCalDav) << "have new remote addition:" << remoteUri;
            remoteChanges->insert(remoteUri);
        }
    }

    qCDebug(lcCalDav) << "Calculated local  A/M/R:" << localAdditions->size() << "/" << localModifications->size()
                      << "/" << localDeletions->size();
    qCDebug(lcCalDav) << "Calculated remote A/M/R:" << (remoteChanges->size() - nRemoteModifications)
                      << "/" << nRemoteModifications << "/" << remoteDeletions->size();

    return true;
}

bool NotebookSyncAgent::updateIncidences(const QList<Buteo::Dav::Resource> &resources)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    mRemoteAdditions.clear();
    mRemoteModifications.clear();
    bool success = true;
    for (const Buteo::Dav::Resource &resource : resources) {
        QByteArray errorMessage;
        KCalendarCore::Incidence::List modifications;
        if (!mLocalStorage->syncFromResource(resource,
                                             &mRemoteAdditions, &modifications,
                                             &errorMessage)) {
            mFailingUpdates.insert(resource.href, errorMessage);
            success = false;
        }
        for (KCalendarCore::Incidence::Ptr incidence : modifications) {
            if (mRemoteChanges.contains(IncidenceHandler::uri(incidence))) {
                // Only stores as modifications the incidences that were noted
                // as remote changes, since we may also update incidences after
                // push when the etag is not part of the push answer.
                mRemoteModifications.append(incidence);
            }
        }
    }

    QStringList failures;
    for (const QString &uri : mFailingUpdates.keys()) {
        QHash<QString, QString>::ConstIterator it = mPulledUids.find(uri);
        if (it != mPulledUids.constEnd()) {
            failures.append(it.value());
        }
    }
    mLocalStorage->flagUpdateFailures(failures);

    return success;
}

bool NotebookSyncAgent::deleteIncidences(const KCalendarCore::Incidence::List deletedIncidences)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;
    bool success = true;
    for (KCalendarCore::Incidence::Ptr incidence : deletedIncidences) {
        if (!mLocalStorage->deleteInstance(incidence->uid(), incidence->recurrenceId())) {
            qCWarning(lcCalDav) << "Unable to delete incidence: " << incidence->uid() << incidence->recurrenceId();
            mFailingUpdates.insert(IncidenceHandler::uri(incidence),
                                   QByteArray("Cannot delete incidence."));
            success = false;
        } else {
            qCDebug(lcCalDav) << "Deleted incidence: " << incidence->uid() << incidence->recurrenceId();
        }
    }
    return success;
}
