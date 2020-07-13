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

#include <SyncResults.h>

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
                               const QString &encodedRemotePath,
                               bool readOnlyFlag = false,
                               QObject *parent = 0);
    ~NotebookSyncAgent();

    bool setNotebookFromInfo(const QString &notebookName,
                             const QString &color,
                             const QString &userEmail,
                             const QString &accountId,
                             const QString &pluginName,
                             const QString &syncProfile);

    void startSync(const QDateTime &fromDateTime,
                   const QDateTime &toDateTime,
                   bool withUpsync, bool withDownsync);

    void abort();
    bool applyRemoteChanges();
    Buteo::TargetResults result() const;
    void finalize();

    bool isFinished() const;
    bool isDeleted() const;
    bool hasDownloadErrors() const;
    bool hasUploadErrors() const;

    const QString& path() const;

signals:
    void finished();

private slots:
    void reportRequestFinished(const QString &uri);
    void additionalReportRequestFinished(const QString &uri);
    void nonReportRequestFinished(const QString &uri);
    void processETags(const QString &uri);
private:
    void sendReportRequest(const QStringList &remoteUris = QStringList());
    void clearRequests();
    void requestFinished(Request *request);

    void fetchRemoteChanges();
    bool updateIncidences(const QList<Reader::CalendarResource> &resources);
    bool deleteIncidences(const KCalCore::Incidence::List deletedIncidences);
    void updateIncidence(KCalCore::Incidence::Ptr incidence,
                         KCalCore::Incidence::Ptr storedIncidence,
                         const KCalCore::Incidence::List instances = KCalCore::Incidence::List());
    bool addIncidence(KCalCore::Incidence::Ptr incidence);
    bool addException(KCalCore::Incidence::Ptr incidence,
                      KCalCore::Incidence::Ptr recurringIncidence,
                      bool ensureRDate = false);
    void updateHrefETag(const QString &uid, const QString &href, const QString &etag) const;

    void sendLocalChanges();
    QString constructLocalChangeIcs(KCalCore::Incidence::Ptr updatedIncidence);
    void finalizeSendingLocalChanges();

    bool calculateDelta(const QHash<QString, QString> &remoteUriEtags,
                        KCalCore::Incidence::List *localAdditions,
                        KCalCore::Incidence::List *localModifications,
                        KCalCore::Incidence::List *localDeletions,
                        QList<QString> *remoteAdditions,
                        QList<QString> *remoteModifications,
                        KCalCore::Incidence::List *remoteDeletions);

    QNetworkAccessManager* mNetworkManager;
    Settings *mSettings;
    QSet<Request *> mRequests;
    mKCal::ExtendedCalendar::Ptr mCalendar;
    mKCal::ExtendedStorage::Ptr mStorage;
    mKCal::Notebook::Ptr mNotebook;
    QDateTime mFromDateTime;
    QDateTime mToDateTime;
    KDateTime mNotebookSyncedDateTime;
    QString mEncodedRemotePath;
    QString mRemoteCalendarPath; // contains calendar path.  resource prefix.  doesn't include host, percent decoded.
    SyncMode mSyncMode;          // quick (etag-based delta detection) or slow (full report) sync
    bool mRetriedReport;         // some servers will fail the first request but succeed on second
    bool mNotebookNeedsDeletion; // if the calendar was deleted remotely, we will need to delete it locally.
    Buteo::TargetResults mResults;
    bool mEnableUpsync, mEnableDownsync;
    bool mReadOnlyFlag;
    bool mHasUploadErrors, mHasDownloadErrors;

    // these are used only in quick-sync mode.
    // delta detection and change data
    KCalCore::Incidence::List mLocalAdditions;
    KCalCore::Incidence::List mLocalModifications;
    KCalCore::Incidence::List mLocalDeletions;
    QList<QString> mRemoteAdditions;
    QList<QString> mRemoteModifications;
    KCalCore::Incidence::List mRemoteDeletions;
    KCalCore::Incidence::List mPurgeList;
    QHash<QString, QString> mSentUids; // Dictionnary of sent (href, uid) made from
                                       // local additions, modifications.
    bool mHasUpdatedEtags;

    // received remote incidence resource data
    QList<Reader::CalendarResource> mReceivedCalendarResources;

    friend class tst_NotebookSyncAgent;
};

#endif // NOTEBOOKSYNCAGENT_P_H
