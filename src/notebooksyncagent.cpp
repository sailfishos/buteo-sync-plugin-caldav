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

#include "logging.h"
#include <SyncResults.h>

#include <KCalendarCore/Incidence>

#include <QDebug>


#define NOTEBOOK_FUNCTION_CALL_TRACE qCDebug(lcCalDavTrace) << Q_FUNC_INFO << (mNotebook ? mNotebook->account() : "")

namespace {
    // mKCal deletes custom properties of deleted incidences.
    // This is problematic for sync, as we need some fields
    // (resource URI and ETAG) in order to sync properly.
    // Hence, we abuse the COMMENTS field of the incidence.
    QString incidenceHrefUri(KCalendarCore::Incidence::Ptr incidence, const QString &remoteCalendarPath = QString(), bool *uriNeedsFilling = 0)
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
                if (uri.isEmpty() && uriNeedsFilling) {
                    qCWarning(lcCalDav) << "Stored uri was empty for:" << incidence->uid() << incidence->recurrenceId().toString();
                    return remoteCalendarPath + incidence->uid() + ".ics";
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
        qCWarning(lcCalDav) << "Returning empty uri for:" << incidence->uid() << incidence->recurrenceId().toString();
        return QString();
    }
    void setIncidenceHrefUri(KCalendarCore::Incidence::Ptr incidence, const QString &hrefUri)
    {
        const QStringList &comments(incidence->comments());
        for (const QString &comment : comments) {
            if (comment.startsWith("buteo:caldav:uri:")
                && incidence->removeComment(comment)) {
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
    QString incidenceETag(KCalendarCore::Incidence::Ptr incidence)
    {
        const QStringList &comments(incidence->comments());
        for (const QString &comment : comments) {
            if (comment.startsWith("buteo:caldav:etag:")) {
                return comment.mid(18);
            }
        }
        return QString();
    }
    void setIncidenceETag(KCalendarCore::Incidence::Ptr incidence, const QString &etag)
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

    void updateIncidenceHrefEtag(KCalendarCore::Incidence::Ptr incidence,
                                 const QString &href, const QString &etag)
    {
        // Set the URI and the ETAG property to the required values.
        qCDebug(lcCalDav) << "Adding URI and ETAG to incidence:" << incidence->uid() << incidence->recurrenceId().toString() << ":" << href << etag;
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

    bool isCopiedDetachedIncidence(KCalendarCore::Incidence::Ptr incidence)
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
                          const KCalendarCore::Incidence::List &incidences,
                          const QString &remotePath = QString())
    {
        for (int i = 0; i < incidences.size(); i++) {
            bool doit = true;
            const QString href = incidenceHrefUri(incidences[i], remotePath, remotePath.isEmpty() ? 0 : &doit);
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

    static const QByteArray app = QByteArrayLiteral("VOLATILE");
    static const QByteArray name = QByteArrayLiteral("SYNC-FAILURE");
    void flagUploadFailure(const QHash<QString, QByteArray> &failingHrefs,
                           const KCalendarCore::Incidence::List &incidences,
                           const QString &remotePath = QString())
    {
        for (int i = 0; i < incidences.size(); i++) {
            bool doit = true;
            if (failingHrefs.contains(incidenceHrefUri(incidences[i], remotePath, remotePath.isEmpty() ? 0 : &doit))) {
                incidences[i]->setCustomProperty(app, name, QStringLiteral("upload"));
            } else {
                incidences[i]->removeCustomProperty(app, name);
            }
        }
    }
    bool isFlaggedAsUploadFailure(const KCalendarCore::Incidence::Ptr &incidence)
    {
        return incidence->customProperty(app, name) == QStringLiteral("upload");
    }
    void flagUpdateSuccess(const KCalendarCore::Incidence::Ptr &incidence)
    {
        incidence->removeCustomProperty(app, name);
    }
    void flagUpdateFailure(const KCalendarCore::Incidence::Ptr &incidence)
    {
        incidence->setCustomProperty(app, name, QStringLiteral("update"));
    }
    void flagDeleteFailure(const KCalendarCore::Incidence::Ptr &incidence)
    {
        incidence->setCustomProperty(app, name, QStringLiteral("delete"));
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
    , mEnableUpsync(true)
    , mEnableDownsync(true)
    , mReadOnlyFlag(readOnlyFlag)
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
static const QByteArray SERVER_COLOR_PROPERTY = QByteArrayLiteral("serverColor");

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
            qCDebug(lcCalDav) << "found notebook:" << notebook->uid() << "for remote calendar:" << mRemoteCalendarPath;
            mNotebook = notebook;
            if (!color.isEmpty()
                && notebook->customProperty(SERVER_COLOR_PROPERTY) != color) {
                if (!notebook->customProperty(SERVER_COLOR_PROPERTY).isEmpty()) {
                    // Override user-selected notebook color only on each server change
                    // and not if there was no server color saved.
                    mNotebook->setColor(color);
                }
                mNotebook->setCustomProperty(SERVER_COLOR_PROPERTY, color);
            }
            mNotebook->setName(notebookName);
            mNotebook->setSyncProfile(syncProfile);
            mNotebook->setCustomProperty(EMAIL_PROPERTY, userEmail);
            mNotebook->setPluginName(pluginName);
            return true;
        }
    }
    qCDebug(lcCalDav) << "no notebook exists for" << mRemoteCalendarPath;
    // or create a new one
    mNotebook = mKCal::Notebook::Ptr(new mKCal::Notebook(notebookName, QString()));
    mNotebook->setAccount(accountId);
    mNotebook->setPluginName(pluginName);
    mNotebook->setSyncProfile(syncProfile);
    mNotebook->setCustomProperty(PATH_PROPERTY, mRemoteCalendarPath);
    mNotebook->setCustomProperty(EMAIL_PROPERTY, userEmail);
    if (!color.isEmpty()) {
        mNotebook->setColor(color);
        mNotebook->setCustomProperty(SERVER_COLOR_PROPERTY, color);
    }
    return true;
}

void NotebookSyncAgent::startSync(const QDateTime &fromDateTime,
                                  const QDateTime &toDateTime,
                                  bool withUpsync, bool withDownsync)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (!mNotebook) {
        qCDebug(lcCalDav) << "no notebook to sync.";
        return;
    }

    // Store sync time before sync is completed to avoid loosing events
    // that may be inserted server side between now and the termination
    // of the process.
    mNotebookSyncedDateTime = QDateTime::currentDateTimeUtc();
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
        qCDebug(lcCalDav) << "Start slow sync for notebook:" << mNotebook->name() << "for account" << mNotebook->account()
                  << "between" << fromDateTime << "to" << toDateTime;
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
        qCDebug(lcCalDav) << "Start quick sync for notebook:" << mNotebook->uid()
                  << "between" << fromDateTime << "to" << toDateTime
                  << ", sync changes since" << mNotebook->syncDate();
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
        mFailingUpdates.insert(uri, QByteArray());
        clearRequests();
        emit finished();
        return;
    }
    qCDebug(lcCalDav) << "report request finished with result:" << report->errorCode() << report->errorMessage();

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        // NOTE: we don't store the remote artifacts yet
        // Instead, we just emit finished (for this notebook)
        // Once ALL notebooks are finished, then we apply the remote changes.
        // This prevents the worst partial-sync issues.
        mReceivedCalendarResources += report->receivedCalendarResources();
        qCDebug(lcCalDav) << "Report request finished: received:"
                  << report->receivedCalendarResources().length() << "iCal blobs";
    } else if (mSyncMode == SlowSync
               && report->networkError() == QNetworkReply::AuthenticationRequiredError
               && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        qCWarning(lcCalDav) << "Retrying REPORT after request failed with QNetworkReply::AuthenticationRequiredError";
        mRetriedReport = true;
        sendReportRequest();
    } else if (mSyncMode == SlowSync
               && report->networkError() == QNetworkReply::ContentNotFoundError) {
        // The remote calendar resource was removed after we created the account but before first sync.
        // We don't perform resource discovery in CalDAV during each sync cycle,
        // so we can have local calendar metadata for remotely removed calendars.
        // In this case, we just skip sync of this calendar, as it was deleted.
        mNotebookNeedsDeletion = true;
        qCDebug(lcCalDav) << "calendar" << uri << "was deleted remotely, skipping sync locally.";
    } else {
        for (const QString href : report->fetchedUris()) {
            mFailingUpdates.insert(href, report->errorData());
        }
        mFailingUpdates.insert(uri, report->errorData());
    }

    requestFinished(report);
}

void NotebookSyncAgent::processETags(const QString &uri)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    Report *report = qobject_cast<Report*>(sender());
    if (!report) {
        mFailingUpdates.insert(uri, QByteArray());
        clearRequests();
        emit finished();
        return;
    }
    qCDebug(lcCalDav) << "fetch etags finished with result:" << report->errorCode() << report->errorMessage();

    if (report->errorCode() == Buteo::SyncResults::NO_ERROR) {
        qCDebug(lcCalDav) << "Process tags for server path" << uri;
        // we have a hash from resource href-uri to resource info (including etags).
        QHash<QString, QString> remoteHrefUriToEtags;
        for (const Reader::CalendarResource &resource :
                   report->receivedCalendarResources()) {
            if (!resource.href.contains(mRemoteCalendarPath)) {
                qCWarning(lcCalDav) << "href does not contain server path:" << resource.href << ":" << mRemoteCalendarPath;
                mFailingUpdates.insert(uri, QByteArray("Mismatch in hrefs from server response."));
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
                            &mRemoteChanges,
                            &mRemoteDeletions)) {
            qCWarning(lcCalDav) << "unable to calculate the sync delta for:" << mRemoteCalendarPath;
            mFailingUpdates.insert(uri, QByteArray("Cannot compute delta."));
            clearRequests();
            emit finished();
            return;
        }

        if (mEnableDownsync && !mRemoteChanges.isEmpty()) {
            // some incidences have changed on the server, so fetch the new details
            sendReportRequest(mRemoteChanges.toList());
        }
        sendLocalChanges();
    } else if (report->networkError() == QNetworkReply::AuthenticationRequiredError && !mRetriedReport) {
        // Yahoo sometimes fails the initial request with an authentication error. Let's try once more
        qCWarning(lcCalDav) << "Retrying ETAG REPORT after request failed with QNetworkReply::AuthenticationRequiredError";
        mRetriedReport = true;
        fetchRemoteChanges();
    } else if (report->networkError() == QNetworkReply::ContentNotFoundError) {
        // The remote calendar resource was removed.
        // We don't perform resource discovery in CalDAV during each sync cycle,
        // so we can have local calendars which mirror remotely-removed calendars.
        // In this situation, we need to delete the local calendar.
        mNotebookNeedsDeletion = true;
        qCDebug(lcCalDav) << "calendar" << uri << "was deleted remotely, marking for deletion locally:" << mNotebook->name();
    } else {
        mFailingUpdates.insert(uri, QByteArray("Cannot fetch selected items."));
    }

    requestFinished(report);
}

void NotebookSyncAgent::sendLocalChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    mFailingUploads.clear();

    if (!mLocalAdditions.count() && !mLocalModifications.count() && !mLocalDeletions.count()) {
        // no local changes to upsync.
        // we're finished syncing.
        qCDebug(lcCalDav) << "no local changes to upsync - finished with notebook" << mNotebook->name() << mRemoteCalendarPath;
        return;
    } else if (!mEnableUpsync) {
        qCDebug(lcCalDav) << "Not upsyncing local changes, upsync disable in profile.";
        return;
    } else if (mReadOnlyFlag) {
        qCDebug(lcCalDav) << "Not upsyncing local changes, upstream read only calendar.";
        return;
    } else {
        qCDebug(lcCalDav) << "upsyncing local changes: A/M/R:" << mLocalAdditions.count() << "/" << mLocalModifications.count() << "/" << mLocalDeletions.count();
    }

    // For deletions, if a persistent exception is deleted we may need to do a PUT
    // containing all of the still-existing events in the series.
    // (Alternative is to push a STATUS:CANCELLED event?)
    // Hence, we first need to find out if any deletion is a lone-persistent-exception deletion.
    QMultiHash<QString, QDateTime> uidToRecurrenceIdDeletions;
    QHash<QString, QString> uidToUri;  // we cannot look up custom properties of deleted incidences, so cache them here.
    for (KCalendarCore::Incidence::Ptr localDeletion : const_cast<const KCalendarCore::Incidence::List&>(mLocalDeletions)) {
        uidToRecurrenceIdDeletions.insert(localDeletion->uid(), localDeletion->recurrenceId());
        uidToUri.insert(localDeletion->uid(), incidenceHrefUri(localDeletion));
    }

    // now send DELETEs as required, and PUTs as required.
    const QStringList keys = uidToRecurrenceIdDeletions.uniqueKeys();
    for (const QString &uid : keys) {
        QList<QDateTime> recurrenceIds = uidToRecurrenceIdDeletions.values(uid);
        if (!recurrenceIds.contains(QDateTime())) {
            mStorage->load(uid);
            KCalendarCore::Incidence::Ptr recurringSeries = mCalendar->incidence(uid);
            if (recurringSeries) {
                mLocalModifications.append(recurringSeries);
                continue; // finished with this deletion.
            } else {
                qCWarning(lcCalDav) << "Unable to load recurring incidence for deleted exception; deleting entire series instead";
                // fall through to the DELETE code below.
            }
        }

        // the whole series is being deleted; can DELETE.
        QString remoteUri = uidToUri.value(uid);
        qCDebug(lcCalDav) << "deleting whole series:" << remoteUri << "with uid:" << uid;
        Delete *del = new Delete(mNetworkManager, mSettings);
        mRequests.insert(del);
        connect(del, &Delete::finished, this, &NotebookSyncAgent::nonReportRequestFinished);
        del->deleteEvent(remoteUri);
    }
    // Incidence will be actually purged only if all operations succeed.
    mPurgeList += mLocalDeletions;

    mSentUids.clear();
    KCalendarCore::Incidence::List toUpload(mLocalAdditions + mLocalModifications);
    for (int i = 0; i < toUpload.count(); i++) {
        bool create = false;
        QString href = incidenceHrefUri(toUpload[i], mRemoteCalendarPath, &create);
        if (mSentUids.contains(href)) {
            qCDebug(lcCalDav) << "Already handled upload" << i << "via series update";
            continue; // already handled this one, as a result of a previous update of another occurrence in the series.
        }
        QString icsData;
        if (toUpload[i]->recurs() || toUpload[i]->hasRecurrenceId()) {
            if (mStorage->loadSeries(toUpload[i]->uid())) {
                KCalendarCore::Incidence::Ptr recurringIncidence(toUpload[i]->recurs() ? toUpload[i] : mCalendar->incidence(toUpload[i]->uid()));
                if (recurringIncidence) {
                    icsData = IncidenceHandler::toIcs(recurringIncidence,
                                                      mCalendar->instances(recurringIncidence));
                } else {
                    qCWarning(lcCalDav) << "Cannot find parent of " << toUpload[i]->uid() << "for upload of series.";
                }
            } else {
                qCWarning(lcCalDav) << "Cannot load series " << toUpload[i]->uid();
            }
        } else {
            icsData = IncidenceHandler::toIcs(toUpload[i]);
        }
        if (icsData.isEmpty()) {
            qCDebug(lcCalDav) << "Skipping upload of broken incidence:" << i << ":" << toUpload[i]->uid();
            mFailingUploads.insert(href, QByteArray("Cannot generate ICS data."));
        } else {
            qCDebug(lcCalDav) << "Uploading incidence" << i << "via PUT for uid:" << toUpload[i]->uid();
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
        mFailingUploads.insert(uri, QByteArray());
        clearRequests();
        emit finished();
        return;
    }
    if (request->errorCode() != Buteo::SyncResults::NO_ERROR) {
        mFailingUploads.insert(uri, request->errorData());
    }

    Put *putRequest = qobject_cast<Put*>(request);
    if (putRequest) {
        if (request->errorCode() == Buteo::SyncResults::NO_ERROR) {
            const QString &etag = putRequest->updatedETag(uri);
            if (!etag.isEmpty()) {
                // Apply Etag and Href changes immediately since incidences are now
                // for sure on server.
                updateHrefETag(mSentUids.take(uri), uri, etag);
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
            KCalendarCore::Incidence::List::Iterator it = mPurgeList.begin();
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
    if (last && !mSentUids.isEmpty()) {
        // mSentUids have been cleared from uids that have already
        // been updated with new etag value. Just remains the ones
        // that requires additional retrieval to get etag values.
        sendReportRequest(mSentUids.keys());
    }

    requestFinished(request);
}

static KCalendarCore::Incidence::List loadAll(mKCal::ExtendedStorage::Ptr storage, mKCal::ExtendedCalendar::Ptr calendar, const KCalendarCore::Incidence::List &incidences)
{
    KCalendarCore::Incidence::List out;
    for (int i = 0; i < incidences.size(); i++){
        if (storage->load(incidences[i]->uid(), incidences[i]->recurrenceId())) {
            const KCalendarCore::Incidence::Ptr incidence = calendar->incidence(incidences[i]->uid(), incidences[i]->recurrenceId());
            if (incidence) {
                out.append(incidence);
            }
        }
    }
    return out;
}

bool NotebookSyncAgent::applyRemoteChanges()
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    if (!mNotebook) {
        qCDebug(lcCalDav) << "Missing notebook in apply changes.";
        return false;
    }
    // mNotebook may not exist in mStorage, because it is new, or
    // database has been modified and notebooks been reloaded.
    mKCal::Notebook::Ptr notebook(mStorage->notebook(mNotebook->uid()));
    if (mEnableDownsync && mNotebookNeedsDeletion) {
        // delete the notebook from local database
        if (notebook && !mStorage->deleteNotebook(notebook)) {
            qCWarning(lcCalDav) << "Cannot delete notebook" << notebook->name() << "from storage.";
            mNotebookNeedsDeletion = false;
        }
        return mNotebookNeedsDeletion;
    }

    // If current notebook is not already in storage, we add it.
    if (!notebook) {
        if (!mStorage->addNotebook(mNotebook)) {
            qCDebug(lcCalDav) << "Unable to (re)create notebook" << mNotebook->name() << "for account" << mNotebook->account() << ":" << mRemoteCalendarPath;
            return false;
        }
        notebook = mNotebook;
    }

    bool success = true;
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
        qCWarning(lcCalDav) << "Cannot purge from database the marked as deleted incidences.";
    }

    notebook->setIsReadOnly(mReadOnlyFlag);
    notebook->setSyncDate(mNotebookSyncedDateTime);
    notebook->setName(mNotebook->name());
    notebook->setColor(mNotebook->color());
    notebook->setSyncProfile(mNotebook->syncProfile());
    notebook->setCustomProperty(PATH_PROPERTY, mRemoteCalendarPath);
    if (!mStorage->updateNotebook(notebook)) {
        qCWarning(lcCalDav) << "Cannot update notebook" << notebook->name() << "in storage.";
        success = false;
    }

    return success;
}

Buteo::TargetResults NotebookSyncAgent::result() const
{
    if (mSyncMode == SlowSync) {
        unsigned int count = 0;
        for (QList<Reader::CalendarResource>::ConstIterator it = mReceivedCalendarResources.constBegin(); it != mReceivedCalendarResources.constEnd(); ++it) {
            if (!mFailingUpdates.contains(it->href)) {
                count += it->incidences.count();
            }
        }
        return Buteo::TargetResults(mNotebook->name().toHtmlEscaped(),
                                    Buteo::ItemCounts(count, 0, 0),
                                    Buteo::ItemCounts());
    } else {
        Buteo::TargetResults results(mNotebook->name().toHtmlEscaped());

        summarizeResults(&results, LOCAL, Buteo::TargetResults::ITEM_ADDED,
                         mFailingUpdates, mRemoteAdditions);
        summarizeResults(&results, LOCAL, Buteo::TargetResults::ITEM_DELETED,
                         mFailingUpdates, mRemoteDeletions);
        summarizeResults(&results, LOCAL, Buteo::TargetResults::ITEM_MODIFIED,
                         mFailingUpdates, mRemoteModifications);
        summarizeResults(&results, REMOTE, Buteo::TargetResults::ITEM_ADDED,
                         mFailingUploads, mLocalAdditions, mRemoteCalendarPath);
        summarizeResults(&results, REMOTE, Buteo::TargetResults::ITEM_DELETED,
                         mFailingUploads, mLocalDeletions);
        summarizeResults(&results, REMOTE, Buteo::TargetResults::ITEM_MODIFIED,
                         mFailingUploads, mLocalModifications);

        return results;
    }
}

void NotebookSyncAgent::requestFinished(Request *request)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    mRequests.remove(request);
    request->deleteLater();

    if (mRequests.isEmpty()) {
        if (!mSentUids.isEmpty()) {
            const QList<Reader::CalendarResource> &resources = mReceivedCalendarResources;
            for (const Reader::CalendarResource &resource : resources) {
                if (mSentUids.contains(resource.href) && resource.etag.isEmpty()) {
                    // Asked for a resource etag but didn't get it.
                    mFailingUploads.insert(resource.href, QByteArray("Unable to retrieve etag."));
                }
            }
        }
        // Flag (or remove flag) for all failing (or not) local changes.
        flagUploadFailure(mFailingUploads, loadAll(mStorage, mCalendar, mLocalAdditions), mRemoteCalendarPath);
        flagUploadFailure(mFailingUploads, loadAll(mStorage, mCalendar, mLocalModifications));

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
    return !mFailingUpdates.isEmpty();
}

bool NotebookSyncAgent::hasUploadErrors() const
{
    return !mFailingUploads.isEmpty();
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
        KCalendarCore::Incidence::List *localAdditions,
        KCalendarCore::Incidence::List *localModifications,
        KCalendarCore::Incidence::List *localDeletions,
        QSet<QString> *remoteChanges,
        KCalendarCore::Incidence::List *remoteDeletions)
{
    // Note that the mKCal API doesn't provide a way to get all deleted/modified incidences
    // for a notebook, as it implements the SQL query using an inequality on both modifiedAfter
    // and createdBefore; so instead we have to build a datetime which "should" satisfy
    // the inequality for all possible local modifications detectable since the last sync.
    QDateTime syncDateTime = mNotebook->syncDate().addSecs(1); // deleted after, created before...

    // load all local incidences
    KCalendarCore::Incidence::List localIncidences;
    if (!mStorage->allIncidences(&localIncidences, mNotebook->uid())) {
        qCWarning(lcCalDav) << "Unable to load notebook incidences, aborting sync of notebook:" << mRemoteCalendarPath << ":" << mNotebook->uid();
        return false;
    }

    // separate them into buckets.
    // note that each remote URI can be associated with multiple local incidences (due recurrenceId incidences)
    // Here we can determine local additions and remote deletions.
    QHash<QString, QString> localUriEtags; // remote uri to the etag we saw last time.
    for (KCalendarCore::Incidence::Ptr incidence : const_cast<const KCalendarCore::Incidence::List&>(localIncidences)) {
        bool modified = (incidence->created() < syncDateTime && incidence->lastModified() >= syncDateTime);
        bool uriWasEmpty = false;
        QString remoteUri = incidenceHrefUri(incidence, mRemoteCalendarPath, &uriWasEmpty);
        if (uriWasEmpty) {
            // must be either a new local addition or a previously-upsynced local addition
            // if we failed to update its uri after the successful upsync.
            if (remoteUriEtags.contains(remoteUri)) { // we saw this on remote side...
                // we previously upsynced this incidence but then connectivity died.
                if (!modified) {
                    qCDebug(lcCalDav) << "have previously partially upsynced local addition, needs uri update:" << remoteUri;
                    // ensure that it will be seen as a remote modification and trigger download for etag and uri update.
                    localUriEtags.insert(remoteUri, QStringLiteral("missing ETag"));
                } else  {
                    qCDebug(lcCalDav) << "have local modification to partially synced incidence:" << incidence->uid() << incidence->recurrenceId().toString();
                    // note: we cannot check the etag to determine if it changed, since we may not have received the updated etag after the partial sync.
                    // we treat this as a "definite" local modification due to the partially-synced status.
                    setIncidenceHrefUri(incidence, remoteUri);
                    setIncidenceETag(incidence, remoteUriEtags.value(remoteUri));
                    localModifications->append(incidence);
                    localUriEtags.insert(remoteUri, incidenceETag(incidence));
                }
            } else { // it doesn't exist on remote side...
                // new local addition.
                qCDebug(lcCalDav) << "have new local addition:" << incidence->uid() << incidence->recurrenceId().toString();
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
                    qCDebug(lcCalDav) << "ignoring out-of-range missing remote incidence:" << incidence->uid() << incidence->recurrenceId().toString();
                } else {
                    qCDebug(lcCalDav) << "have remote deletion of previously synced incidence:" << incidence->uid() << incidence->recurrenceId().toString();
                    // Ignoring local modifications if any.
                    remoteDeletions->append(incidence);
                }
            } else if (isCopiedDetachedIncidence(incidence)) {
                if (incidenceETag(incidence) == remoteUriEtags.value(remoteUri)) {
                    qCDebug(lcCalDav) << "Found new locally-added persistent exception:" << incidence->uid() << incidence->recurrenceId().toString() << ":" << remoteUri;
                    localAdditions->append(incidence);
                } else {
                    qCDebug(lcCalDav) << "ignoring new locally-added persistent exception to remotely modified incidence:" << incidence->uid() << incidence->recurrenceId().toString() << ":" << remoteUri;
                    mUpdatingList.append(incidence);
                }
            } else if (incidenceETag(incidence) != remoteUriEtags.value(remoteUri)) {
                mUpdatingList.append(incidence);
                // Ignoring local modifications if any.
            } else if (modified) {
                // this is a real local modification.
                qCDebug(lcCalDav) << "have local modification:" << incidence->uid() << incidence->recurrenceId().toString();
                localModifications->append(incidence);
            } else if (isFlaggedAsUploadFailure(incidence)) {
                // this one failed to upload last time, we retry it.
                qCDebug(lcCalDav) << "have failing to upload incidence:" << incidence->uid() << incidence->recurrenceId().toString();
                localModifications->append(incidence);
            }
            localUriEtags.insert(remoteUri, incidenceETag(incidence));
        }
    }

    // List all local deletions reported by mkcal.
    KCalendarCore::Incidence::List deleted;
    if (!mStorage->deletedIncidences(&deleted, QDateTime(), mNotebook->uid())) {
        qCWarning(lcCalDav) << "mKCal::ExtendedStorage::deletedIncidences() failed";
        return false;
    }
    for (KCalendarCore::Incidence::Ptr incidence : const_cast<const KCalendarCore::Incidence::List&>(deleted)) {
        bool uriWasEmpty = false;
        QString remoteUri = incidenceHrefUri(incidence, mRemoteCalendarPath, &uriWasEmpty);
        if (remoteUriEtags.contains(remoteUri)) {
            if (uriWasEmpty) {
                // we originally upsynced this pure-local addition, but then connectivity was
                // lost before we updated the uid of it locally to include the remote uri.
                // subsequently, the user deleted the incidence.
                // Hence, it exists remotely, and has been deleted locally.
                qCDebug(lcCalDav) << "have local deletion for partially synced incidence:" << incidence->uid() << incidence->recurrenceId().toString();
                // We treat this as a local deletion.
                setIncidenceHrefUri(incidence, remoteUri);
                setIncidenceETag(incidence, remoteUriEtags.value(remoteUri));
                localDeletions->append(incidence);
            } else {
                if (incidenceETag(incidence) == remoteUriEtags.value(remoteUri)) {
                    // the incidence was previously synced successfully.  it has now been deleted locally.
                    qCDebug(lcCalDav) << "have local deletion for previously synced incidence:" << incidence->uid() << incidence->recurrenceId().toString();
                    localDeletions->append(incidence);
                } else {
                    // Sub-optimal case for persistent exceptions.
                    // TODO: improve handling of this case.
                    qCDebug(lcCalDav) << "ignoring local deletion due to remote modification:"
                              << incidence->uid() << incidence->recurrenceId().toString();
                    mPurgeList.append(incidence);
                }
            }
            localUriEtags.insert(remoteUri, incidenceETag(incidence));
        } else {
            // it was either already deleted remotely, or was never upsynced from the local prior to deletion.
            qCDebug(lcCalDav) << "ignoring local deletion of non-existent remote incidence:" << incidence->uid() << incidence->recurrenceId().toString() << "at" << remoteUri;
            mPurgeList.append(incidence);
        }
    }

    // now determine remote additions and modifications.
    QSet<QString> remoteAdditions, remoteModifications;
    const QStringList keys = remoteUriEtags.keys();
    for (const QString &remoteUri : keys) {
        if (!localUriEtags.contains(remoteUri)) {
            qCDebug(lcCalDav) << "have new remote addition:" << remoteUri;
            remoteAdditions.insert(remoteUri);
        } else if (localUriEtags.value(remoteUri) != remoteUriEtags.value(remoteUri)) {
            // etag changed; this is a server-side modification.
            qCDebug(lcCalDav) << "have remote modification to previously synced incidence at:" << remoteUri;
            qCDebug(lcCalDav) << "previously seen ETag was:" << localUriEtags.value(remoteUri) << "-> new ETag is:" << remoteUriEtags.value(remoteUri);
            remoteModifications.insert(remoteUri);
        } else {
            // this incidence is unchanged since last sync.
            qCDebug(lcCalDav) << "unchanged server-side since last sync:" << remoteUri;
        }
    }
    *remoteChanges = remoteAdditions + remoteModifications;

    qCDebug(lcCalDav) << "Calculated local  A/M/R:" << localAdditions->size() << "/" << localModifications->size() << "/" << localDeletions->size();
    qCDebug(lcCalDav) << "Calculated remote A/M/R:" << remoteAdditions.size() << "/" << remoteModifications.size() << "/" << remoteDeletions->size();

    return true;
}

static QString nbUid(const QString &notebookId, const QString &uid)
{
    return QStringLiteral("NBUID:%1:%2").arg(notebookId).arg(uid);
}

static KCalendarCore::Incidence::Ptr loadIncidence(mKCal::ExtendedStorage::Ptr storage, mKCal::ExtendedCalendar::Ptr calendar, const QString &notebookId, const QString &uid)
{
    const QString &nbuid = nbUid(notebookId, uid);

    // Load from storage any matching incidence by uid or modified uid.
    // Use series loading to ensure that mCalendar->instances() are successful.
    storage->loadSeries(uid);
    storage->loadSeries(nbuid);

    KCalendarCore::Incidence::Ptr incidence = calendar->incidence(uid);
    if (!incidence) {
        incidence = calendar->incidence(nbuid);
    }
    return incidence;
}

void NotebookSyncAgent::updateIncidence(KCalendarCore::Incidence::Ptr incidence,
                                        KCalendarCore::Incidence::Ptr storedIncidence)
{
    if (incidence->status() == KCalendarCore::Incidence::StatusCanceled
        || incidence->customStatus().compare(QStringLiteral("CANCELLED"), Qt::CaseInsensitive) == 0) {
        qCDebug(lcCalDav) << "Queuing existing event for deletion:" << storedIncidence->uid() << storedIncidence->recurrenceId().toString();
        mLocalDeletions.append(incidence);
    } else {
        qCDebug(lcCalDav) << "Updating existing event:" << storedIncidence->uid() << storedIncidence->recurrenceId().toString();
        storedIncidence->startUpdates();
        *storedIncidence.staticCast<KCalendarCore::IncidenceBase>() = *incidence.staticCast<KCalendarCore::IncidenceBase>();

        flagUpdateSuccess(storedIncidence);

        storedIncidence->endUpdates();
        // Avoid spurious detections of modified incidences
        // by ensuring that the received last modification date time
        // is previous to the sync date time.
        if (storedIncidence->lastModified() > mNotebookSyncedDateTime) {
            storedIncidence->setLastModified(mNotebookSyncedDateTime.addSecs(-2));
        }

        if (mRemoteChanges.contains(incidenceHrefUri(storedIncidence))) {
            // Only stores as modifications the incidences that were noted
            // as remote changes, since we may also update incidences after
            // push when the etag is not part of the push answer.
            mRemoteModifications.append(storedIncidence);
        }
    }
}

bool NotebookSyncAgent::addIncidence(KCalendarCore::Incidence::Ptr incidence)
{
    qCDebug(lcCalDav) << "Adding new incidence:" << incidence->uid() << incidence->recurrenceId().toString();
    mRemoteAdditions.append(incidence);

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
    if (!mCalendar->setDefaultNotebook(mNotebook->uid())) {
        qCWarning(lcCalDav) << "Cannot set default notebook to " << mNotebook->uid();
    }
    return mCalendar->addIncidence(incidence);
}

bool NotebookSyncAgent::addException(KCalendarCore::Incidence::Ptr incidence,
                                     KCalendarCore::Incidence::Ptr recurringIncidence,
                                     bool ensureRDate)
{
    if (ensureRDate && recurringIncidence->allDay()
        && !recurringIncidence->recursOn(incidence->recurrenceId().date(),
                                         incidence->recurrenceId().timeZone())) {
        recurringIncidence->recurrence()->addRDate(incidence->recurrenceId().date());
    } else if (ensureRDate && !recurringIncidence->allDay()
               && !recurringIncidence->recursAt(incidence->recurrenceId())) {
        recurringIncidence->recurrence()->addRDateTime(incidence->recurrenceId());
    }

    return addIncidence(incidence);
}

bool NotebookSyncAgent::updateIncidences(const QList<Reader::CalendarResource> &resources)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;

    mRemoteAdditions.clear();
    mRemoteModifications.clear();

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
                qCWarning(lcCalDav) << "Updated incidence list contains incidences with non-matching uids!";
                return false; // this is always an error.  each resource corresponds to a single event series.
            }
            if (!resource.incidences[i]->hasRecurrenceId()) {
                parentIndex = i;
            }
            updateIncidenceHrefEtag(resource.incidences[i], resource.href, resource.etag);
        }

        qCDebug(lcCalDav) << "Saving the added/updated base incidence before saving persistent exceptions:" << uid;
        KCalendarCore::Incidence::Ptr localBaseIncidence =
            loadIncidence(mStorage, mCalendar, mNotebook->uid(), uid);
        if (localBaseIncidence) {
            if (parentIndex >= 0) {
                resource.incidences[parentIndex]->setUid(localBaseIncidence->uid());
                updateIncidence(resource.incidences[parentIndex], localBaseIncidence);
            }
        } else {
            if (parentIndex == -1) {
                // construct a recurring parent series for these orphans.
                localBaseIncidence = KCalendarCore::Incidence::Ptr(resource.incidences.first()->clone());
                localBaseIncidence->setRecurrenceId(QDateTime());
            } else {
                localBaseIncidence = resource.incidences[parentIndex];
            }
            localBaseIncidence->setUid(nbUid(mNotebook->uid(), uid));
            if (addIncidence(localBaseIncidence)) {
                localBaseIncidence = loadIncidence(mStorage, mCalendar, mNotebook->uid(), uid);
            } else {
                localBaseIncidence = KCalendarCore::Incidence::Ptr();
            }
        }
        if (!localBaseIncidence) {
            qCWarning(lcCalDav) << "Error saving base incidence of resource" << resource.href;
            mFailingUpdates.insert(resource.href, QByteArray("Cannot create local parent."));
            success = false;
            continue; // don't return false and block the entire sync cycle, just ignore this event.
        }

        // update persistent exceptions which are in the remote list.
        QList<QDateTime> remoteRecurrenceIds;
        for (int i = 0; i < resource.incidences.size(); ++i) {
            KCalendarCore::Incidence::Ptr remoteInstance = resource.incidences[i];
            if (!remoteInstance->hasRecurrenceId()) {
                continue; // already handled this one.
            }
            remoteRecurrenceIds.append(remoteInstance->recurrenceId());

            qCDebug(lcCalDav) << "Now saving a persistent exception:" << remoteInstance->recurrenceId().toString();
            remoteInstance->setUid(localBaseIncidence->uid());
            KCalendarCore::Incidence::Ptr localInstance = mCalendar->incidence(remoteInstance->uid(), remoteInstance->recurrenceId());
            if (localInstance) {
                updateIncidence(remoteInstance, localInstance);
            } else if (!addException(remoteInstance, localBaseIncidence, parentIndex == -1)) {
                qCWarning(lcCalDav) << "Error saving updated persistent occurrence of resource" << resource.href << ":" << remoteInstance->recurrenceId().toString();
                mFailingUpdates.insert(resource.href, QByteArray("Cannot create exception."));
                success = false;
                continue; // don't return false and block the entire sync cycle, just ignore this event.
            }
        }

        // remove persistent exceptions which are not in the remote list.
        KCalendarCore::Incidence::List localInstances;
        if (localBaseIncidence->recurs())
            localInstances = mCalendar->instances(localBaseIncidence);
        for (int i = 0; i < localInstances.size(); ++i) {
            KCalendarCore::Incidence::Ptr localInstance = localInstances[i];
            if (!remoteRecurrenceIds.contains(localInstance->recurrenceId())) {
                qCDebug(lcCalDav) << "Schedule for removal persistent occurrence:" << localInstance->recurrenceId().toString();
                // Will be deleted in the call to deleteIncidences
                mRemoteDeletions.append(localInstance);
            }
        }
    }

    if (!mFailingUpdates.isEmpty()) {
        for (int i = 0; i < mUpdatingList.size(); i++){
            if (mFailingUpdates.contains(incidenceHrefUri(mUpdatingList[i]))) {
                const QString uid = mUpdatingList[i]->uid();
                const QDateTime recid = mUpdatingList[i]->recurrenceId();
                KCalendarCore::Incidence::Ptr incidence = mCalendar->incidence(uid, recid);
                if (!incidence && mStorage->load(uid, recid)) {
                    incidence = mCalendar->incidence(uid, recid);
                }
                if (incidence) {
                    flagUpdateFailure(incidence);
                }
            }
        }
    }

    return success;
}

bool NotebookSyncAgent::deleteIncidences(const KCalendarCore::Incidence::List deletedIncidences)
{
    NOTEBOOK_FUNCTION_CALL_TRACE;
    bool success = true;
    for (KCalendarCore::Incidence::Ptr doomed : deletedIncidences) {
        mStorage->load(doomed->uid(), doomed->recurrenceId());
        if (!mCalendar->deleteIncidence(mCalendar->incidence(doomed->uid(), doomed->recurrenceId()))) {
            qCWarning(lcCalDav) << "Unable to delete incidence: " << doomed->uid() << doomed->recurrenceId().toString();
            mFailingUpdates.insert(incidenceHrefUri(doomed), QByteArray("Cannot delete incidence."));
            flagDeleteFailure(doomed);
            success = false;
        } else {
            qCDebug(lcCalDav) << "Deleted incidence: " << doomed->uid() << doomed->recurrenceId().toString();
        }
    }
    return success;
}

void NotebookSyncAgent::updateHrefETag(const QString &uid, const QString &href, const QString &etag) const
{
    if (!mStorage->loadSeries(uid)) {
        qCWarning(lcCalDav) << "Unable to load incidence from database:" << uid;
        return;
    }

    KCalendarCore::Incidence::Ptr localBaseIncidence = mCalendar->incidence(uid);
    if (localBaseIncidence) {
        updateIncidenceHrefEtag(localBaseIncidence, href, etag);
        localBaseIncidence->updated();
        if (localBaseIncidence->recurs()) {
            const KCalendarCore::Incidence::List instances = mCalendar->instances(localBaseIncidence);
            for (const KCalendarCore::Incidence::Ptr &instance : instances) {
                updateIncidenceHrefEtag(instance, href, etag);
                instance->updated();
            }
        }
    } else {
        qCWarning(lcCalDav) << "Unable to find base incidence: " << uid;
    }
}
