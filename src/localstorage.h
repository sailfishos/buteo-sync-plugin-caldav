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

#ifndef LOCALSTORAGE_H
#define LOCALSTORAGE_H

#include <davtypes.h>

#include <QSharedPointer>

#include <extendedstorage.h>

class LocalStorage
{
public:
    LocalStorage(int accountId, const QString &remotePath);
    ~LocalStorage();

    QDateTime lastSyncDate() const;
    bool ensureNotebook();
    bool deleteNotebook();
    void setNotebookMetadata(const Buteo::Dav::CalendarInfo &info,
                             const QString &userEmail,
                             const QString &pluginName,
                             const QString &syncProfile);
    bool snapshot(KCalendarCore::Incidence::List *allIncidences,
                  QSet<QString> *modifiedInstances,
                  KCalendarCore::Incidence::List *deletedIncidences);

    Buteo::Dav::Resource toResource(KCalendarCore::Incidence::Ptr incidence);
    QSet<QString> toDeletedResources(KCalendarCore::Incidence::List deletedIncidences,
                                     KCalendarCore::Incidence::List *modifiedIncidences);
    void resourceIsSynced(const QString &uri, const QString &etag, const QString &uid);

    bool syncFromResource(const Buteo::Dav::Resource &resource,
                          KCalendarCore::Incidence::List *additions,
                          KCalendarCore::Incidence::List *modifications,
                          QByteArray *errorMessage);
    void flagUpdateFailures(const QStringList &uids);
    void flagUploadFailures(const QStringList &uids);

    bool deleteInstance(const QString &uid, const QDateTime &recurrenceId);
    bool purgeDeleted(const KCalendarCore::Incidence::List &incidences);

    bool save();

    static bool deleteNotebooksForAccount(unsigned int accountId);
    static bool deleteOrphanedNotebooks(const QList<unsigned int> &knownAccountIds,
                                        const QString &service);

private:
    static QString nbUid(const QString &notebookId, const QString &uid);
    static KCalendarCore::Incidence::List parseICSData(const QString &data);

    mKCal::ExtendedStorage::Ptr mStorage;
    mKCal::Notebook mNotebook;

    friend class tst_Reader;
    friend class tst_NotebookSyncAgent;
};

#endif
