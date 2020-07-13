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

#include <incidence.h>
#include <event.h>
#include <todo.h>
#include <journal.h>
#include <attendee.h>

#include <QDebug>


#define NOTEBOOK_FUNCTION_CALL_TRACE FUNCTION_CALL_TRACE(QLatin1String(Q_FUNC_INFO) + " " + (mNotebook ? mNotebook->account() : ""))

namespace {
    // mKCal deletes custom properties of deleted incidences.
    // This is problematic for sync, as we need some fields
    // (resource URI and ETAG) in order to sync properly.
    // Hence, we abuse the COMMENTS field of the incidence.
    QString incidenceHrefUri(KCalCore::Incidence::Ptr incidence, const QString &remoteCalendarPath = QString(), bool *uriNeedsFilling = 0)
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
        for (const QString &comment : comments) {
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
        for (const QString &comment : comments) {
            if (comment.startsWith("buteo:caldav:etag:")) {
                return comment.mid(18);
            }
        }
        return QString();
    }
    void setIncidenceETag(KCalCore::Incidence::Ptr incidence, const QString &etag)
    {
        const QStringList &comments(incidence->comments());
        for (const QString &comment : comments) {
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
            // Add a flag to distinguish persistent exceptions that have
            // been detached during the sync process (with the flag)
            // or by a call to dissociateSingleOccurrence() outside
            // of the sync process (in that later case, the incidence
            // will have to be treated as a local addition of a persistent
            // exception, see the calculateDelta() function).
            incidence->removeComment("buteo:caldav:detached-and-synced");
            incidence->addComment("buteo:caldav:detached-and-synced");
        }
    }

    bool isCopiedDetachedIncidence(KCalCore::Incidence::Ptr incidence)
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

    bool incidenceWithin(KCalCore::Incidence::Ptr incidence,
                         const QDateTime &from, const QDateTime &to)
    {
        return incidence->dtStart().dateTime() <= to
            && (!incidence->recurs()
                || !incidence->recurrence()->endDateTime().isValid()
                || incidence->recurrence()->endDateTime().dateTime() >= from)
            && (incidence->recurs()
                || incidence->dateTime(KCalCore::Incidence::RoleDisplayEnd).dateTime() >= from);
    }
}


NotebookSyncAgent::NotebookSyncAgent(mKCal::ExtendedCalendar::Ptr calendar,
                                     mKCal::ExtendedStorage::Ptr storage,
                                     QNetworkAccessManager *networkAccessManager,
                                     Settings *settings,
                                     const QString &encodedRemotePath,
                                     bool readOnlyFlag,
                                     QObject *parent)
    : QObject(parent)
    , mNetworkManager(networkAccessManager)
    , mSettings(settings)
    , mCalendar(calendar)
    , mStorage(storage)
    , mNotebook(0)
    , mEncodedRemotePath(encodedRemotePath)
    , mSyncMode(NoSyncMode)
    , mRetriedReport(false)
    , mNotebookNeedsDeletion(false)
    , mResults(QString(), Buteo::ItemCounts(), Buteo::ItemCounts())
    , mEnableUpsync(true)
    , mEnableDownsync(true)
    , mReadOnlyFlag(readOnlyFlag)
    , mHasUploadErrors(false)
    , mHasDownloadErrors(false)
    , mHasUpdatedEtags(false)
{
    // the calendar path may be percent-encoded.  Return UTF-8 QString.
    mRemoteCalendarPath = QUrl::fromPercentEncoding(mEncodedRemotePath.toUtf8());
    // Yahoo! seems to double-percent-encode for some reason
    if (mSettings->serverAddress().contains(QStringLiteral("caldav.calendar.yahoo.com"))) {
        mRemoteCalendarPath = QUrl::fromPercentEncoding(mRemoteCalendarPath.toUtf8());
    }
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

static const QByteArray PATH_PROPERTY = QByteArrayLiteral("remoteCalendarPath");
static const QByteArray EMAIL_PROPERTY = QByteArrayLiteral("userPrincipalEmail");
bool NotebookSyncAgent::setNotebookFromInfo(const QString &notebookName,
                                            const QString &color,
                                            const QString &userEmail,
                                            const QString &accountId,
                                            const QString &pluginName,
                                            const QString &syncProfile)
{
    mNotebook = static_cast<mKCal::Notebook::Ptr>(0);
    // Look for an already existing notebook in storage for this account and path.
    const mKCal::Notebook::List notebooks = mStorage->notebooks();
    for (mKCal::Notebook::Ptr notebook : notebooks) {
        if (notebook->account() == accountId
            && (notebook->customProperty(PATH_PROPERTY) == mRemoteCalendarPath
                || notebook->syncProfile().endsWith(QStringLiteral(":%1").arg(mRemoteCalendarPath)))) {
            LOG_DEBUG("found notebook:" << notebook->uid() << "for remote calendar:" << mRemoteCalendarPath);
            mNotebook = notebook;
            mNotebook->setColor(color);
            mNotebook->setName(notebookName);
            mNotebook->setSyncProfile(syncProfile);
            mNotebook->setCustomProperty(EMAIL_PROPERTY, userEmail);
            mNotebook->setPluginName(pluginName);
            return true;
        }
    }
    LOG_DEBUG("no notebook exists for" << mRemoteCalendarPath);
    // or create a new one
    mNotebook = mKCal::Notebook::Ptr(new mKCal::Notebook(notebookName, QString()));
    mNotebook->setAccount(accountId);
    mNotebook->setPluginName(pluginName);
    mNotebook->setSyncProfile(syncProfile);
    mNotebook->setCustomProperty(PATH_PROPERTY, mRemoteCalendarPath);
    mNotebook->setCustomProperty(EMAIL_PROPERTY, userEmail);
    mNotebook->setColor(color);
    return true;
}

void NotebookSyncAgent::startSync(const QDateTime &fromDateTime,
                                  const QDateTime &toDateTime,
                                  bool withUpsync, bool withDownsync)
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
    mEnableUpsync = withUpsync;
    mEnableDownsync = withDownsync;
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
        LOG_DEBUG("Start quick sync for notebook:" << mNotebook->uid()
                  << "between" << fromDateTime << "to" << toDateTime
                  << ", sync changes since" << mNotebook->syncDate().dateTime());
        mSyncMode = QuickSync;

        fetchRemoteChanges();
    }
}

void NotebookSyncAgent::sendReportRequest(const QStringList &remoteUris)
{
    // must be m_syncMode = SlowSync.
    Report *report = new Report(mNetworkManager, mSettings);
    mRequests.insert(report);
    connect(report, &Report::finished, this, &NotebookSyncAgent::reportRequestFinished);
    if (remoteUris.isEmpty()) {
        report->getAllEvents(mRemoteCalendarPath, mFromDateTime, mToDateTime);
    } else {
        report->multiGetEvents(mRemoteCalendarPath, remoteUris);
    }
}

void NotebookSyncAgent::fetchRemoteChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // must be m_syncMode = QuickSync.
    Report *report = new Report(mNetworkManager, mSettings);
    mRequests.insert(report);
    connect(report, &Report::finished, this, &NotebookSyncAgent::processETags);
    report->getAllETags(mRemoteCalendarPath, mFromDateTime, mToDateTime);
}

void NotebookSyncAgent::reportRequestFinished(const QString &uri)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = qobject_cast<Report*>(sender());
    if (!report) {
        mHasDownloadErrors = true;
        clearRequests();
        emit finished();
        return;
    }
    LOG_DEBUG("report request finished with result:" << report->errorCode() << report->errorString());

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        // NOTE: we don't store the remote artifacts yet
        // Instead, we just emit finished (for this notebook)
        // Once ALL notebooks are finished, then we apply the remote changes.
        // This prevents the worst partial-sync issues.
        mReceivedCalendarResources += report->receivedCalendarResources();
        unsigned int count = 0;
        for (QList<Reader::CalendarResource>::ConstIterator it = report->receivedCalendarResources().constBegin(); it != report->receivedCalendarResources().constEnd(); ++it) {
            count += it->incidences.count();
        }
        LOG_DEBUG("Report request finished: received:"
                  << report->receivedCalendarResources().length() << "iCal blobs containing a total of"
                  << count << "incidences");

        if (mSyncMode == SlowSync) {
            mResults = Buteo::TargetResults(mNotebook->name().toHtmlEscaped(),
                                            Buteo::ItemCounts(count, 0, 0),
                                            Buteo::ItemCounts());
        }
    } else if (mSyncMode == SlowSync
               && report->networkError() == QNetworkReply::AuthenticationRequiredError
               && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        LOG_WARNING("Retrying REPORT after request failed with QNetworkReply::AuthenticationRequiredError");
        mRetriedReport = true;
        sendReportRequest();
    } else if (mSyncMode == SlowSync
               && report->networkError() == QNetworkReply::ContentNotFoundError) {
        // The remote calendar resource was removed after we created the account but before first sync.
        // We don't perform resource discovery in CalDAV during each sync cycle,
        // so we can have local calendar metadata for remotely removed calendars.
        // In this case, we just skip sync of this calendar, as it was deleted.
        mNotebookNeedsDeletion = true;
        LOG_DEBUG("calendar" << uri << "was deleted remotely, skipping sync locally.");
    } else {
        mHasDownloadErrors = true;
    }

    requestFinished(report);
}

void NotebookSyncAgent::processETags(const QString &uri)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = qobject_cast<Report*>(sender());
    if (!report) {
        mHasDownloadErrors = true;
        clearRequests();
        emit finished();
        return;
    }
    LOG_DEBUG("fetch etags finished with result:" << report->errorCode() << report->errorString());

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        LOG_DEBUG("Process tags for server path" << uri);
        // we have a hash from resource href-uri to resource info (including etags).
        QHash<QString, QString> remoteHrefUriToEtags;
        for (const Reader::CalendarResource &resource :
                   report->receivedCalendarResources()) {
            if (!resource.href.contains(mRemoteCalendarPath)) {
                LOG_WARNING("href does not contain server path:" << resource.href << ":" << mRemoteCalendarPath);
                mHasDownloadErrors = true;
                clearRequests();
                emit finished();
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
            mHasDownloadErrors = true;
            clearRequests();
            emit finished();
            return;
        }
        mResults = Buteo::TargetResults
            (mNotebook->name().toHtmlEscaped(),
             Buteo::ItemCounts(mRemoteAdditions.size(),
                               mRemoteDeletions.size(),
                               mRemoteModifications.size()),
             Buteo::ItemCounts(mLocalAdditions.size(),
                               mLocalDeletions.size(),
                               mLocalModifications.size()));

        // Note that due to the fact that we update the ETAG and URI data in locally
        // upsynced events during sync, those incidences will be reported as modified
        // during the next sync cycle (even though the only changes may have been
        // that ETAG+URI change).  Hence, we need to fetch all of those again, and
        // then manually check equivalence (ignoring etag+uri value) with remote copy.
        // Also fetch updated and new items full data if required.
        QStringList fetchRemoteHrefUris = mRemoteAdditions + mRemoteModifications;
        if (mEnableDownsync && !fetchRemoteHrefUris.isEmpty()) {
            // some incidences have changed on the server, so fetch the new details
            sendReportRequest(fetchRemoteHrefUris);
        }
        sendLocalChanges();
    } else if (report->networkError() == QNetworkReply::AuthenticationRequiredError && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        LOG_WARNING("Retrying ETAG REPORT after request failed with QNetworkReply::AuthenticationRequiredError");
        mRetriedReport = true;
        fetchRemoteChanges();
    } else if (report->networkError() == QNetworkReply::ContentNotFoundError) {
        // The remote calendar resource was removed.
        // We don't perform resource discovery in CalDAV during each sync cycle,
        // so we can have local calendars which mirror remotely-removed calendars.
        // In this situation, we need to delete the local calendar.
        mNotebookNeedsDeletion = true;
        LOG_DEBUG("calendar" << uri << "was deleted remotely, marking for deletion locally:" << mNotebook->name());
    } else {
        mHasDownloadErrors = true;
    }

    requestFinished(report);
}

void NotebookSyncAgent::sendLocalChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (!mLocalAdditions.count() && !mLocalModifications.count() && !mLocalDeletions.count()) {
        // no local changes to upsync.
        // we're finished syncing.
        LOG_DEBUG("no local changes to upsync - finished with notebook" << mNotebook->name() << mRemoteCalendarPath);
        return;
    } else if (!mEnableUpsync) {
        LOG_DEBUG("Not upsyncing local changes, upsync disable in profile.");
        return;
    } else if (mReadOnlyFlag) {
        LOG_DEBUG("Not upsyncing local changes, upstream read only calendar.");
        return;
    } else {
        LOG_DEBUG("upsyncing local changes: A/M/R:" << mLocalAdditions.count() << "/" << mLocalModifications.count() << "/" << mLocalDeletions.count());
    }

    // For deletions, if a persistent exception is deleted we may need to do a PUT
    // containing all of the still-existing events in the series.
    // (Alternative is to push a STATUS:CANCELLED event?)
    // Hence, we first need to find out if any deletion is a lone-persistent-exception deletion.
    QMultiHash<QString, KDateTime> uidToRecurrenceIdDeletions;
    QHash<QString, QString> uidToUri;  // we cannot look up custom properties of deleted incidences, so cache them here.
    for (KCalCore::Incidence::Ptr localDeletion : const_cast<const KCalCore::Incidence::List&>(mLocalDeletions)) {
        uidToRecurrenceIdDeletions.insert(localDeletion->uid(), localDeletion->recurrenceId());
        uidToUri.insert(localDeletion->uid(), incidenceHrefUri(localDeletion));
    }

    // now send DELETEs as required, and PUTs as required.
    const QStringList keys = uidToRecurrenceIdDeletions.uniqueKeys();
    for (const QString &uid : keys) {
        QList<KDateTime> recurrenceIds = uidToRecurrenceIdDeletions.values(uid);
        if (!recurrenceIds.contains(KDateTime())) {
            mStorage->load(uid);
            KCalCore::Incidence::Ptr recurringSeries = mCalendar->incidence(uid);
            if (recurringSeries) {
                mLocalModifications.append(recurringSeries);
                continue; // finished with this deletion.
            } else {
                LOG_WARNING("Unable to load recurring incidence for deleted exception; deleting entire series instead");
                // fall through to the DELETE code below.
            }
        }

        // the whole series is being deleted; can DELETE.
        QString remoteUri = uidToUri.value(uid);
        LOG_DEBUG("deleting whole series:" << remoteUri << "with uid:" << uid);
        Delete *del = new Delete(mNetworkManager, mSettings);
        mRequests.insert(del);
        connect(del, &Delete::finished, this, &NotebookSyncAgent::nonReportRequestFinished);
        del->deleteEvent(remoteUri);
    }
    // Incidence will be actually purged only if all operations succeed.
    mPurgeList += mLocalDeletions;

    mSentUids.clear();
    KCalCore::Incidence::List toUpload(mLocalAdditions + mLocalModifications);
    for (int i = 0; i < toUpload.count(); i++) {
        bool create = false;
        QString href = incidenceHrefUri(toUpload[i], mRemoteCalendarPath, &create);
        if (href.isEmpty()) {
            LOG_WARNING("Unable to determine remote uri for incidence:" << toUpload[i]->uid());
            mHasUploadErrors = true;
            continue;
        }
        if (mSentUids.contains(href)) {
            LOG_DEBUG("Already handled upload" << i << "via series update");
            continue; // already handled this one, as a result of a previous update of another occurrence in the series.
        }
        QString icsData;
        if (toUpload[i]->recurs() || toUpload[i]->hasRecurrenceId()) {
            if (mStorage->loadSeries(toUpload[i]->uid())) {
                KCalCore::Incidence::Ptr recurringIncidence(toUpload[i]->recurs() ? toUpload[i] : mCalendar->incidence(toUpload[i]->uid()));
                if (recurringIncidence) {
                    icsData = IncidenceHandler::toIcs(recurringIncidence,
                                                      mCalendar->instances(recurringIncidence));
                } else {
                    LOG_WARNING("Cannot find parent of " << toUpload[i]->uid() << "for upload of series.");
                }
            } else {
                LOG_WARNING("Cannot load series " << toUpload[i]->uid());
            }
        } else {
            icsData = IncidenceHandler::toIcs(toUpload[i]);
        }
        if (icsData.isEmpty()) {
            LOG_DEBUG("Skipping upload of broken incidence:" << i << ":" << toUpload[i]->uid());
            mHasUploadErrors = true;
        } else {
            LOG_DEBUG("Uploading incidence" << i << "via PUT for uid:" << toUpload[i]->uid());
            Put *put = new Put(mNetworkManager, mSettings);
            mRequests.insert(put);
            connect(put, &Put::finished, this, &NotebookSyncAgent::nonReportRequestFinished);
            put->sendIcalData(href, icsData, incidenceETag(toUpload[i]));
            mSentUids.insert(href, toUpload[i]->uid());
        }
    }
}

void NotebookSyncAgent::nonReportRequestFinished(const QString &uri)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Request *request = qobject_cast<Request*>(sender());
    if (!request) {
        mHasUploadErrors = true;
        clearRequests();
        emit finished();
        return;
    }
    mHasUploadErrors = mHasUploadErrors
        || (request->errorCode() != Buteo::SyncResults::NO_ERROR);

    Put *putRequest = qobject_cast<Put*>(request);
    if (putRequest) {
        if (request->errorCode() == Buteo::SyncResults::NO_ERROR) {
            const QString &etag = putRequest->updatedETag(uri);
            if (!etag.isEmpty()) {
                // Apply Etag and Href changes immediately since incidences are now
                // for sure on server.
                updateHrefETag(mSentUids.take(uri), uri, etag);
                mHasUpdatedEtags = true;
            }
        } else {
            // Don't try to get etag later for a failed upload.
            mSentUids.remove(uri);
        }
    }
    Delete *deleteRequest = qobject_cast<Delete*>(request);
    if (deleteRequest) {
        if (request->errorCode() != Buteo::SyncResults::NO_ERROR) {
            // Don't purge yet the locally deleted incidence.
            KCalCore::Incidence::List::Iterator it = mPurgeList.begin();
            while (it != mPurgeList.end()) {
                if (incidenceHrefUri(*it) == uri) {
                    it = mPurgeList.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    bool last = true;
    for (QSet<Request*>::ConstIterator it = mRequests.constBegin();
         it != mRequests.constEnd(); ++it) {
        last = last && (*it == request ||
                        (!qobject_cast<Put*>(*it) && !qobject_cast<Delete*>(*it)));
    }
    if (last) {
        finalizeSendingLocalChanges();
    }

    requestFinished(request);
}

void NotebookSyncAgent::finalizeSendingLocalChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // All PUT requests have been finished, some etags may have
    // been updated already. Try to save them in storage.
    if (mHasUpdatedEtags && !mStorage->save()) {
        LOG_WARNING("Unable to save calendar storage after etag changes!");
    }

    // mSentUids have been cleared from uids that have already
    // been updated with new etag value. Just remains the ones
    // that requires additional retrieval to get etag values.
    if (!mSentUids.isEmpty()) {
        Report *report = new Report(mNetworkManager, mSettings);
        mRequests.insert(report);
        connect(report, &Report::finished, this, &NotebookSyncAgent::additionalReportRequestFinished);
        report->multiGetEvents(mRemoteCalendarPath, mSentUids.keys());
    }
}

void NotebookSyncAgent::additionalReportRequestFinished(const QString &uri)
{
    Q_UNUSED(uri);
    NOTEBOOK_FUNCTION_CALL_TRACE;

    // The server did not originally respond with the update ETAG values after
    // our initial PUT/UPDATE so we had to do an addition report request.
    // This response will contain the new ETAG values for any resource we
    // upsynced (ie, a local modification/addition) and also the incidence
    // as it may have been modified by the server.

    Report *report = qobject_cast<Report*>(sender());

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        LOG_DEBUG("Additional report request finished: received:"
                  << report->receivedCalendarResources().count() << "incidences");
        mReceivedCalendarResources += report->receivedCalendarResources();
    } else {
        LOG_WARNING("Additional report request finished with error.");
        mHasDownloadErrors = true;
    }

    requestFinished(report);
}

bool NotebookSyncAgent::applyRemoteChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (mHasDownloadErrors && !mReceivedCalendarResources.size()) {
        LOG_DEBUG("Download had errors, nothing to apply.");
        return false;
    }

    if (!mNotebook) {
        LOG_DEBUG("Missing notebook in apply changes.");
        return false;
    }
    // mNotebook may not exist in mStorage, because it is new, or
    // database has been modified and notebooks been reloaded.
    mKCal::Notebook::Ptr notebook(mStorage->notebook(mNotebook->uid()));
    if (mEnableDownsync && mNotebookNeedsDeletion) {
        // delete the notebook from local database
        if (notebook && !mStorage->deleteNotebook(notebook)) {
            LOG_WARNING("Cannot delete notebook" << notebook->name() << "from storage.");
            mNotebookNeedsDeletion = false;
        }
        return mNotebookNeedsDeletion;
    }

    // If current notebook is not already in storage, we add it.
    if (!notebook) {
        if (!mStorage->addNotebook(mNotebook)) {
            LOG_DEBUG("Unable to (re)create notebook" << mNotebook->name() << "for account" << mNotebook->account() << ":" << mRemoteCalendarPath);
            return false;
        }
        notebook = mNotebook;
    }

    bool success = !mHasDownloadErrors;
    // Make notebook writable for the time of the modifications.
    notebook->setIsReadOnly(false);
    if ((mEnableDownsync || mSyncMode == SlowSync)
        && !updateIncidences(mReceivedCalendarResources)) {
        success = false;
    }
    if (mEnableDownsync && !deleteIncidences(mRemoteDeletions)) {
        success = false;
    }
    // Update storage, before possibly changing readOnly flag for this notebook.
    if (!mStorage->save(mKCal::ExtendedStorage::PurgeDeleted)) {
        success = false;
    }
    if (!mPurgeList.isEmpty() && !mStorage->purgeDeletedIncidences(mPurgeList)) {
        // Silently ignore failed purge action in database.
        LOG_WARNING("Cannot purge from database the marked as deleted incidences.");
    }

    notebook->setIsReadOnly(mReadOnlyFlag);
    notebook->setSyncDate(mNotebookSyncedDateTime);
    notebook->setName(mNotebook->name());
    notebook->setColor(mNotebook->color());
    notebook->setSyncProfile(mNotebook->syncProfile());
    notebook->setCustomProperty(PATH_PROPERTY, mRemoteCalendarPath);
    if (!mStorage->updateNotebook(notebook)) {
        LOG_WARNING("Cannot update notebook" << notebook->name() << "in storage.");
        success = false;
    }

    return success;
}

Buteo::TargetResults NotebookSyncAgent::result() const
{
    return mResults;
}

void NotebookSyncAgent::requestFinished(Request *request)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    mRequests.remove(request);
    request->deleteLater();

    if (mRequests.isEmpty()) {
        emit finished();
    }
}

void NotebookSyncAgent::finalize()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;
}

bool NotebookSyncAgent::isFinished() const
{
    return mRequests.isEmpty();
}

bool NotebookSyncAgent::isDeleted() const
{
    return (mEnableDownsync && mNotebookNeedsDeletion);
}

bool NotebookSyncAgent::hasDownloadErrors() const
{
    return mHasDownloadErrors;
}

bool NotebookSyncAgent::hasUploadErrors() const
{
    return mHasUploadErrors;
}

const QString& NotebookSyncAgent::path() const
{
    return mEncodedRemotePath;
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
        return false;
    }

    // separate them into buckets.
    // note that each remote URI can be associated with multiple local incidences (due recurrenceId incidences)
    // Here we can determine local additions and remote deletions.
    QHash<QString, QString> localUriEtags; // remote uri to the etag we saw last time.
    for (KCalCore::Incidence::Ptr incidence : const_cast<const KCalCore::Incidence::List&>(localIncidences)) {
        bool uriWasEmpty = false;
        QString remoteUri = incidenceHrefUri(incidence, mRemoteCalendarPath, &uriWasEmpty);
        if (uriWasEmpty) {
            // must be either a new local addition or a previously-upsynced local addition
            // if we failed to update its uri after the successful upsync.
            if (remoteUriEtags.contains(remoteUri)) { // we saw this on remote side...
                // previously partially upsynced, needs uri update.
                LOG_DEBUG("have previously partially upsynced local addition, needs uri update:" << remoteUri);
                // ensure that it will be seen as a remote modification and trigger download
                localUriEtags.insert(remoteUri, QStringLiteral("missing ETag"));
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
                if (!incidenceWithin(incidence, mFromDateTime, mToDateTime)) {
                    LOG_DEBUG("ignoring out-of-range missing remote incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                } else {
                    LOG_DEBUG("have remote deletion of previously synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                    remoteDeletions->append(incidence);
                }
            } else if (isCopiedDetachedIncidence(incidence)) {
                if (incidenceETag(incidence) == remoteUriEtags.value(remoteUri)) {
                    LOG_DEBUG("Found new locally-added persistent exception:" << incidence->uid() << incidence->recurrenceId().toString() << ":" << remoteUri);
                    localAdditions->append(incidence);
                } else {
                    LOG_DEBUG("ignoring new locally-added persistent exception to remotely modified incidence:" << incidence->uid() << incidence->recurrenceId().toString() << ":" << remoteUri);
                }
            }
            localUriEtags.insert(remoteUri, incidenceETag(incidence));
        }
    }

    // Now determine local deletions reported by mkcal since the last sync date.
    KCalCore::Incidence::List deleted;
    if (!mStorage->deletedIncidences(&deleted, syncDateTime, mNotebook->uid())) {
        LOG_WARNING("mKCal::ExtendedStorage::deletedIncidences() failed");
        return false;
    }
    for (KCalCore::Incidence::Ptr incidence : const_cast<const KCalCore::Incidence::List&>(deleted)) {
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
            localUriEtags.insert(remoteUri, incidenceETag(incidence));
        } else {
            // it was either already deleted remotely, or was never upsynced from the local prior to deletion.
            LOG_DEBUG("ignoring local deletion of non-existent remote incidence:" << incidence->uid() << incidence->recurrenceId().toString() << "at" << remoteUri);
            mPurgeList.append(incidence);
        }
    }

    // Now determine local modifications.
    KCalCore::Incidence::List modified;
    if (!mStorage->modifiedIncidences(&modified, syncDateTime, mNotebook->uid())) {
        LOG_WARNING("mKCal::ExtendedStorage::modifiedIncidences() failed");
        return false;
    }
    for (KCalCore::Incidence::Ptr incidence : const_cast<const KCalCore::Incidence::List&>(modified)) {
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
                localUriEtags.insert(remoteUri, incidenceETag(incidence));
            } else if (localAdditions->contains(incidence)) {
                LOG_DEBUG("ignoring local modification to locally added incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                continue;
            } else {
                LOG_DEBUG("ignoring local modification to remotely removed partially-synced incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                continue;
            }
        } else {
            // we have a modification to a previously-synced incidence.
            if (!remoteUriEtags.contains(remoteUri)) {
                LOG_DEBUG("ignoring local modification to remotely deleted incidence:" << incidence->uid() << incidence->recurrenceId().toString());
            } else {
                // determine if the remote etag is still the same.
                // if it is not, then the incidence was modified server-side.
                if (incidenceETag(incidence) != remoteUriEtags.value(remoteUri)) {
                    // if the etags are different, then the event was also modified remotely.
                    // we only support PreferRemote conflict resolution, so we discard the local modification.
                    LOG_DEBUG("ignoring local modification to remotely modified incidence:" << incidence->uid() << incidence->recurrenceId().toString());
                    // Don't append it here, it will be appended later when treating remote modifications.
                    // remoteModifications->append(remoteUri);
                } else {
                    // this is a real local modification.
                    LOG_DEBUG("have local modification:" << incidence->uid() << incidence->recurrenceId().toString());
                    localModifications->append(incidence);
                }
            }
        }
    }

    // now determine remote additions and modifications.
    const QStringList keys = remoteUriEtags.keys();
    for (const QString &remoteUri : keys) {
        if (!localUriEtags.contains(remoteUri)) {
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
        } else if (localUriEtags.value(remoteUri) != remoteUriEtags.value(remoteUri)) {
            // etag changed; this is a server-side modification.
            LOG_DEBUG("have remote modification to previously synced incidence at:" << remoteUri);
            LOG_DEBUG("previously seen ETag was:" << localUriEtags.value(remoteUri) << "-> new ETag is:" << remoteUriEtags.value(remoteUri));
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

static QString nbUid(const QString &notebookId, const QString &uid)
{
    return QStringLiteral("NBUID:%1:%2").arg(notebookId).arg(uid);
}

static KCalCore::Incidence::Ptr loadIncidence(mKCal::ExtendedStorage::Ptr storage, mKCal::ExtendedCalendar::Ptr calendar, const QString &notebookId, const QString &uid)
{
    const QString &nbuid = nbUid(notebookId, uid);

    // Load from storage any matching incidence by uid or modified uid.
    // Use series loading to ensure that mCalendar->instances() are successful.
    storage->loadSeries(uid);
    storage->loadSeries(nbuid);

    KCalCore::Incidence::Ptr incidence = calendar->incidence(uid);
    if (!incidence) {
        incidence = calendar->incidence(nbuid);
    }
    return incidence;
}

void NotebookSyncAgent::updateIncidence(KCalCore::Incidence::Ptr incidence,
                                        KCalCore::Incidence::Ptr storedIncidence,
                                        const KCalCore::Incidence::List instances)
{
    if (incidence->status() == KCalCore::Incidence::StatusCanceled
        || incidence->customStatus().compare(QStringLiteral("CANCELLED"), Qt::CaseInsensitive) == 0) {
        LOG_DEBUG("Queuing existing event for deletion:" << storedIncidence->uid() << storedIncidence->recurrenceId().toString());
        mLocalDeletions.append(incidence);
    } else {
        LOG_DEBUG("Updating existing event:" << storedIncidence->uid() << storedIncidence->recurrenceId().toString());
        storedIncidence->startUpdates();
        *storedIncidence.staticCast<KCalCore::IncidenceBase>() = *incidence.staticCast<KCalCore::IncidenceBase>();

        // if this incidence is a recurring incidence, we should get all persistent occurrences
        // and add them back as EXDATEs.  This is because mkcal expects that dissociated
        // single instances will correspond to an EXDATE, but most sync servers do not (and
        // so will not include the RECURRENCE-ID values as EXDATEs of the parent).
        for (KCalCore::Incidence::Ptr instance : instances) {
            if (instance->hasRecurrenceId()) {
                storedIncidence->recurrence()->addExDateTime(instance->recurrenceId());
            }
        }

        storedIncidence->endUpdates();
        // Avoid spurious detections of modified incidences
        // by ensuring that the received last modification date time
        // is previous to the sync date time.
        if (incidence->lastModified() < mNotebookSyncedDateTime) {
            storedIncidence->setLastModified(incidence->lastModified());
        } else {
            storedIncidence->setLastModified(mNotebookSyncedDateTime.addSecs(-2));
        }
    }
}

bool NotebookSyncAgent::addIncidence(KCalCore::Incidence::Ptr incidence)
{
    LOG_DEBUG("Adding new incidence:" << incidence->uid() << incidence->recurrenceId().toString());
    // To avoid spurious appearings of added events when later
    // calling addedIncidences() and modifiedIncidences(), we
    // set the creation date and modification date by hand, since
    // libical has put them to now when they don't exist.
    if (incidence->created() > mNotebookSyncedDateTime) {
        incidence->setCreated(mNotebookSyncedDateTime.addSecs(-2));
    }
    if (incidence->lastModified() > mNotebookSyncedDateTime) {
        incidence->setLastModified(incidence->created());
    }

    // Set-up the default notebook when adding new incidences.
    mCalendar->addNotebook(mNotebook->uid(), true);
    mCalendar->setDefaultNotebook(mNotebook->uid());
    return mCalendar->addIncidence(incidence);
}

bool NotebookSyncAgent::addException(KCalCore::Incidence::Ptr incidence,
                                     KCalCore::Incidence::Ptr recurringIncidence,
                                     bool ensureRDate)
{
    KDateTime modified = recurringIncidence->lastModified();
    if (ensureRDate && recurringIncidence->allDay()
        && !recurringIncidence->recursOn(incidence->recurrenceId().date(),
                                         incidence->recurrenceId().timeSpec())) {
        recurringIncidence->recurrence()->addRDate(incidence->recurrenceId().date());
    } else if (ensureRDate && !recurringIncidence->allDay()
               && !recurringIncidence->recursAt(incidence->recurrenceId())) {
        recurringIncidence->recurrence()->addRDateTime(incidence->recurrenceId());
    }
    // This modification is due to internal of mkcal storing
    // exception recurrenceId as ex-dates.
    if (recurringIncidence->allDay()) {
        recurringIncidence->recurrence()->addExDate(incidence->recurrenceId().date());
    } else {
        recurringIncidence->recurrence()->addExDateTime(incidence->recurrenceId());
    }
    // Don't update the modification date of the parent.
    recurringIncidence->setLastModified(modified);

    return addIncidence(incidence);
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

    bool success = true;
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
        // find the recurring incidence (parent) in the update list, and save it.
        // alternatively, it may be a non-recurring base incidence.
        const QString uid = resource.incidences.first()->uid();
        int parentIndex = -1;
        for (int i = 0; i < resource.incidences.size(); ++i) {
            if (!resource.incidences[i] || resource.incidences[i]->uid() != uid) {
                LOG_WARNING("Updated incidence list contains incidences with non-matching uids!");
                return false; // this is always an error.  each resource corresponds to a single event series.
            }
            if (!resource.incidences[i]->hasRecurrenceId()) {
                parentIndex = i;
            }
            updateIncidenceHrefEtag(resource.incidences[i], resource.href, resource.etag);
        }

        LOG_DEBUG("Saving the added/updated base incidence before saving persistent exceptions:" << uid);
        KCalCore::Incidence::List localInstances;
        KCalCore::Incidence::Ptr localBaseIncidence =
            loadIncidence(mStorage, mCalendar, mNotebook->uid(), uid);
        if (localBaseIncidence) {
            if (parentIndex >= 0) {
                if (localBaseIncidence->recurs()) {
                    // load the local (persistent) occurrences of the series.
                    // Later we will update or remove them as required.
                    localInstances = mCalendar->instances(localBaseIncidence);
                }
                resource.incidences[parentIndex]->setUid(localBaseIncidence->uid());
                updateIncidence(resource.incidences[parentIndex], localBaseIncidence, localInstances);
            }
        } else {
            if (parentIndex == -1) {
                // construct a recurring parent series for these orphans.
                localBaseIncidence = KCalCore::Incidence::Ptr(resource.incidences.first()->clone());
                localBaseIncidence->setRecurrenceId(KDateTime());
            } else {
                localBaseIncidence = resource.incidences[parentIndex];
            }
            localBaseIncidence->setUid(nbUid(mNotebook->uid(), uid));
            if (addIncidence(localBaseIncidence)) {
                localBaseIncidence = loadIncidence(mStorage, mCalendar, mNotebook->uid(), uid);
            } else {
                localBaseIncidence = KCalCore::Incidence::Ptr();
            }
        }
        if (!localBaseIncidence) {
            LOG_WARNING("Error saving base incidence of resource" << resource.href);
            success = false;
            continue; // don't return false and block the entire sync cycle, just ignore this event.
        }

        // update persistent exceptions which are in the remote list.
        QList<KDateTime> remoteRecurrenceIds;
        for (int i = 0; i < resource.incidences.size(); ++i) {
            KCalCore::Incidence::Ptr remoteInstance = resource.incidences[i];
            if (!remoteInstance->hasRecurrenceId()) {
                continue; // already handled this one.
            }
            remoteRecurrenceIds.append(remoteInstance->recurrenceId());

            LOG_DEBUG("Now saving a persistent exception:" << remoteInstance->recurrenceId().toString());
            remoteInstance->setUid(localBaseIncidence->uid());
            KCalCore::Incidence::Ptr localInstance = mCalendar->incidence(remoteInstance->uid(), remoteInstance->recurrenceId());
            if (localInstance) {
                updateIncidence(remoteInstance, localInstance);
            } else if (!addException(remoteInstance, localBaseIncidence, parentIndex == -1)) {
                LOG_WARNING("Error saving updated persistent occurrence of resource" << resource.href << ":" << remoteInstance->recurrenceId().toString());
                success = false;
                continue; // don't return false and block the entire sync cycle, just ignore this event.
            }
        }

        // remove persistent exceptions which are not in the remote list.
        for (int i = 0; i < localInstances.size(); ++i) {
            KCalCore::Incidence::Ptr localInstance = localInstances[i];
            if (!remoteRecurrenceIds.contains(localInstance->recurrenceId())) {
                LOG_DEBUG("Now removing remotely-removed persistent occurrence:" << localInstance->recurrenceId().toString());
                if (!mCalendar->deleteIncidence(localInstance)) {
                    LOG_WARNING("Error removing remotely deleted persistent occurrence of resource" << resource.href << ":" << localInstance->recurrenceId().toString());
                    // don't return here and block the entire sync cycle.
                    success = false;
                }
            }
        }
    }

    return success;
}

bool NotebookSyncAgent::deleteIncidences(const KCalCore::Incidence::List deletedIncidences)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;
    bool success = true;
    for (KCalCore::Incidence::Ptr doomed : deletedIncidences) {
        mStorage->load(doomed->uid(), doomed->recurrenceId());
        if (!mCalendar->deleteIncidence(mCalendar->incidence(doomed->uid(), doomed->recurrenceId()))) {
            LOG_WARNING("Unable to delete incidence: " << doomed->uid() << doomed->recurrenceId().toString());
            success = false;
        } else {
            LOG_DEBUG("Deleted incidence: " << doomed->uid() << doomed->recurrenceId().toString());
        }
    }
    return success;
}

void NotebookSyncAgent::updateHrefETag(const QString &uid, const QString &href, const QString &etag) const
{
    if (!mStorage->loadSeries(uid)) {
        LOG_WARNING("Unable to load incidence from database:" << uid);
        return;
    }

    KCalCore::Incidence::Ptr localBaseIncidence = mCalendar->incidence(uid);
    if (localBaseIncidence) {
        KDateTime modificationDt = localBaseIncidence->lastModified();
        updateIncidenceHrefEtag(localBaseIncidence, href, etag);
        localBaseIncidence->updated();
        localBaseIncidence->setLastModified(modificationDt);
        if (localBaseIncidence->recurs()) {
            const KCalCore::Incidence::List instances = mCalendar->instances(localBaseIncidence);
            for (const KCalCore::Incidence::Ptr &instance : instances) {
                KDateTime instanceDt = instance->lastModified();
                updateIncidenceHrefEtag(instance, href, etag);
                instance->updated();
                instance->setLastModified(instanceDt);
            }
        }
    } else {
        LOG_WARNING("Unable to find base incidence: " << uid);
    }
}
