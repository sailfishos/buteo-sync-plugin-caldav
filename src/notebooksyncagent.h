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
#ifndef NOTEBOOKSYNCAGENT_P_H
#define NOTEBOOKSYNCAGENT_P_H

#include "reader.h"

#include <extendedcalendar.h>
#include <extendedstorage.h>

#include <QDateTime>

class QNetworkAccessManager;
class Request;
class Settings;

class NotebookSyncAgent : public QObject
{
    Q_OBJECT
public:
    enum SyncMode {
        NoSyncMode,
        SlowSync,   // download everything
        QuickSync   // updates only
    };

    explicit NotebookSyncAgent(mKCal::ExtendedCalendar::Ptr calendar,
                               mKCal::ExtendedStorage::Ptr storage,
                               QNetworkAccessManager *networkAccessManager,
                               Settings *settings,
                               const QString &remoteCalendarPath,
                               QObject *parent = 0);
    ~NotebookSyncAgent();

    bool setNotebookFromInfo(const QString &notebookName,
                             const QString &color,
                             const QString &accountId,
                             const QString &pluginName,
                             const QString &syncProfile);

    void startSync(const QDateTime &fromDateTime,
                   const QDateTime &toDateTime);

    void abort();
    bool applyRemoteChanges();
    void finalize();

    bool isFinished() const;

signals:
    void finished(int minorErrorCode, const QString &message);

private slots:
    void reportRequestFinished();
    void additionalReportRequestFinished();
    void nonReportRequestFinished();
    void processETags();
private:
    void sendReportRequest();
    void clearRequests();
    void emitFinished(int minorErrorCode, const QString &message);

    void fetchRemoteChanges(const QDateTime &fromDateTime, const QDateTime &toDateTime);
    bool updateIncidences(const QList<Reader::CalendarResource> &resources);
    bool updateIncidence(KCalCore::Incidence::Ptr incidence, const QString &resourceHref,
                         const QString &resourceEtag, bool isKnownOrphan, bool *criticalError);
    bool deleteIncidences(KCalCore::Incidence::List deletedIncidences);
    void updateHrefETag(const QString &uid, const QString &href, const QString &etag) const;

    void sendLocalChanges();
    QString constructLocalChangeIcs(KCalCore::Incidence::Ptr updatedIncidence);
    void finalizeSendingLocalChanges();

    // we cannot access custom properties of deleted incidences from mkcal
    // so instead, we need to determine the remote etag and uri via remote etags request and cache it here.
    struct LocalDeletion {
        LocalDeletion(KCalCore::Incidence::Ptr deleted, const QString &etag, const QString &uri)
            : deletedIncidence(deleted), remoteEtag(etag), hrefUri(uri) {}
        KCalCore::Incidence::Ptr deletedIncidence;
        QString remoteEtag;
        QString hrefUri;
    };
    bool calculateDelta(const QHash<QString, QString> &remoteUriEtags,
                        KCalCore::Incidence::List *localAdditions,
                        KCalCore::Incidence::List *localModifications,
                        QList<LocalDeletion> *localDeletions,
                        QList<QString> *remoteAdditions,
                        QList<QString> *remoteModifications,
                        KCalCore::Incidence::List *remoteDeletions);
    void removePossibleLocalModificationIfIdentical(const QString &remoteUri,
                                                    const QList<KDateTime> &recurrenceIds,
                                                    const Reader::CalendarResource &remoteResource,
                                                    const QMultiHash<QString, KDateTime> &addedPersistentExceptionOccurrences,
                                                    KCalCore::Incidence::List *localModifications);

    QNetworkAccessManager* mNetworkManager;
    Settings *mSettings;
    QSet<Request *> mRequests;
    mKCal::ExtendedCalendar::Ptr mCalendar;
    mKCal::ExtendedStorage::Ptr mStorage;
    mKCal::Notebook::Ptr mNotebook;
    QDateTime mFromDateTime;
    QDateTime mToDateTime;
    KDateTime mNotebookSyncedDateTime;
    QString mRemoteCalendarPath; // contains calendar path.  resource prefix.  doesn't include host.
    SyncMode mSyncMode;          // quick (etag-based delta detection) or slow (full report) sync
    bool mRetriedReport;         // some servers will fail the first request but succeed on second
    bool mNotebookNeedsDeletion; // if the calendar was deleted remotely, we will need to delete it locally.
    bool mFinished;

    // these are used only in quick-sync mode.
    // delta detection and change data
    QMultiHash<QString, KDateTime> mAddedPersistentExceptionOccurrences;   // remoteUri to recurrenceIds.
    QMultiHash<QString, KDateTime> mPossibleLocalModificationIncidenceIds; // remoteUri to recurrenceIds.
    KCalCore::Incidence::List mLocalAdditions;
    KCalCore::Incidence::List mLocalModifications;
    QList<LocalDeletion> mLocalDeletions;
    QList<QString> mRemoteAdditions;
    QList<QString> mRemoteModifications;
    KCalCore::Incidence::List mRemoteDeletions;
    QHash<QString, QString> mSentUids; // Dictionnary of sent (href, uid) made from
                                       // local additions, modifications.

    // received remote incidence resource data
    QList<Reader::CalendarResource> mReceivedCalendarResources;

    friend class tst_NotebookSyncAgent;
};

#endif // NOTEBOOKSYNCAGENT_P_H
