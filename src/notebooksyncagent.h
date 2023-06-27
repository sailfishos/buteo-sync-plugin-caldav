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

#include <notebook.h>
#include <calendarstorage.h>

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

    explicit NotebookSyncAgent(QNetworkAccessManager *networkAccessManager,
                               Settings *settings,
                               const QString &encodedRemotePath,
                               bool readOnlyFlag = false,
                               QObject *parent = 0);
    ~NotebookSyncAgent();

    bool setNotebookFromInfo(const mKCal::Notebook::List &systemNotebooks,
                             const QString &notebookName,
                             const QString &color,
                             const QString &userEmail,
                             bool allowEvents, bool allowTodos, bool allowJournals,
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
    bool isCompleted() const;
    bool isDeleted() const;
    bool hasDownloadErrors() const;
    bool hasUploadErrors() const;

    const QString& path() const;
    mKCal::Notebook::Ptr notebook() const;

signals:
    void finished();

private slots:
    void reportRequestFinished(const QString &uri);
    void nonReportRequestFinished(const QString &uri);
    void processETags(const QString &uri);
private:
    void sendReportRequest(const QStringList &remoteUris = QStringList());
    void requestFinished(Request *request);
    void setFatal(const QString &uri, const QByteArray &errorData);

    void fetchRemoteChanges();
    bool updateIncidences(const QList<Reader::CalendarResource> &resources);
    bool deleteIncidences(const KCalendarCore::Incidence::List deletedIncidences);
    void updateIncidence(KCalendarCore::Incidence::Ptr incidence,
                         KCalendarCore::Incidence::Ptr storedIncidence);
    bool addIncidence(KCalendarCore::Incidence::Ptr incidence);
    bool addException(KCalendarCore::Incidence::Ptr incidence,
                      KCalendarCore::Incidence::Ptr recurringIncidence,
                      bool ensureRDate = false);
    void updateHrefETag(const QString &uid, const QString &href, const QString &etag) const;

    void sendLocalChanges();
    QString constructLocalChangeIcs(KCalendarCore::Incidence::Ptr updatedIncidence);

    bool calculateDelta(const QHash<QString, QString> &remoteUriEtags,
                        KCalendarCore::Incidence::List *localAdditions,
                        KCalendarCore::Incidence::List *localModifications,
                        KCalendarCore::Incidence::List *localDeletions,
                        QSet<QString> *remoteChanges,
                        KCalendarCore::Incidence::List *remoteDeletions);

    QNetworkAccessManager* mNetworkManager;
    Settings *mSettings;
    QSet<Request *> mRequests;
    mKCal::CalendarStorage::Ptr mStorage;
    KCalendarCore::MemoryCalendar::Ptr mCalendar;
    QDateTime mFromDateTime;
    QDateTime mToDateTime;
    QDateTime mNotebookSyncedDateTime;
    QString mEncodedRemotePath;
    QString mRemoteCalendarPath; // contains calendar path.  resource prefix.  doesn't include host, percent decoded.
    SyncMode mSyncMode;          // quick (etag-based delta detection) or slow (full report) sync
    bool mRetriedReport;         // some servers will fail the first request but succeed on second
    bool mNotebookNeedsDeletion; // if the calendar was deleted remotely, we will need to delete it locally.
    bool mEnableUpsync, mEnableDownsync;
    bool mReadOnlyFlag;

    // these are used only in quick-sync mode.
    // delta detection and change data
    KCalendarCore::Incidence::List mLocalAdditions;
    KCalendarCore::Incidence::List mLocalModifications;
    KCalendarCore::Incidence::List mLocalDeletions;
    QSet<QString> mRemoteChanges; // Set of URLs to be downloaded
    KCalendarCore::Incidence::List mRemoteDeletions;
    KCalendarCore::Incidence::List mRemoteAdditions;
    KCalendarCore::Incidence::List mRemoteModifications;
    KCalendarCore::Incidence::List mPurgeList;
    KCalendarCore::Incidence::List mUpdatingList; // Incidences corresponding to mRemoteModifications
    QHash<QString, QString> mSentUids; // Dictionnary of sent (href, uid) made from
                                       // local additions, modifications.
    QHash<QString, QByteArray> mFailingUploads; // List of hrefs with upload errors, with the server response.
    QHash<QString, QByteArray> mFailingUpdates; // List of hrefs from which incidences failed to update.
    QString mFatalUri; // A key from mFailingUpdates that prevents the sync to complete.

    // received remote incidence resource data
    QList<Reader::CalendarResource> mReceivedCalendarResources;

    friend class tst_NotebookSyncAgent;
};

#endif // NOTEBOOKSYNCAGENT_P_H
