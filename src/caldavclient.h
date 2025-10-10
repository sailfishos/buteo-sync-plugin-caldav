/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2013 - 2021 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Mani Chandrasekar <maninc@gmail.com>
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

#ifndef CALDAVCLIENT_H
#define CALDAVCLIENT_H

#include "buteo-caldav-plugin.h"
#include "authhandler.h"
#include "notebooksyncagent.h"

#include <QList>
#include <QSet>
#include <QScopedPointer>
#include <QSharedPointer>

#include <davclient.h>
#include <davtypes.h>

#include <extendedstorage.h>

#include <ClientPlugin.h>
#include <SyncResults.h>
#include <SyncCommonDefs.h>
#include <SyncPluginLoader.h>

#include <Accounts/Manager>

#include <sailfishkeyprovider_processmutex.h>

class QNetworkAccessManager;
class Request;

/*
    This plugin allows buteo to sync events with an online CalDAV server. Changes are read from
    and written to the local mkcal database. The plugin integrates with the accounts&sso
    libraries to sync events specific to particular user accounts.

    To perform a sync with this plugin, you need:
        - An online CalDAV server
        - An account created through the accounts&sso libraries
        - A buteo profile

    For example, to load the calendar at https://www.myserver.com/myusername/calendars/My_Calendar/
    you would:

    1) Create an accounts&sso account

    2) Add a service to the account, e.g. 'my_caldav_service'

       The service settings must have the appropriate credentials and sign-in values,
       including a "CredentialsId", so that the CalDavClient can log in through the
       accounts sign-in framework.

       Also, the service needs some setting values to list the calendars to be synchronized:
       For example:
            server_address = https://www.myserver.com
            calendars = ["/myusername/calendars/My_Calendar/"]
            calendar_display_names = ["My Personal Calendar"]
            calendar_colors = ["#ff0000"]
            enabled_calendars = ["/myusername/calendars/My_Calendar/"]

        Multiple calendars may be listed:
            calendars = ["/path/to/calendarA", "/path/to/calendarB"]
            calendar_display_names = ["Calendar A", "Calendar B"]
            calendar_colors = ["#ff0000", "#0000ff"]
            enabled_calendars = ["/path/to/calendarB"]      <-- only calendarB will be synced

     3) Add a buteo profile to <Sync::syncCacheDir()>/.cache/msyncd/sync/.
        Use the supplied src/xmls/sync/caldav-sync.xml as a template.

        Additionally, the profile needs:
            - an "accountid" <key> element with the account id
            - an "account_service_name" <key> element with the account service name

        For example, if the ID of the account from step 1) is '55':

        <?xml version="1.0" encoding="UTF-8"?>
        <profile name="caldav-sync-55" type="sync">
            <key name="accountid" value="55"/>
            <key name="account_service_name" value="my_caldav_service"/>
            <key name="destinationtype" value="online"/>
            <key name="displayname" value="my profile"/>
            <key name="enabled" value="true"/>
            <key name="hidden" value="true"/>
            <key name="use_accounts" value="true"/>

            <profile name="caldav" type="client">
                <key name="Sync Direction" value="two-way"/>
                <key name="Sync Protocol" value="caldav"/>
                <key name="Sync Transport" value="HTTP"/>
                <key name="conflictpolicy" value="prefer remote"/>
            </profile>
        </profile>


     Once you have all these, the profile can be synced via dbus using the profile name. E.g.

        dbus-send --session --type=method_call --print-reply \
            --dest=com.meego.msyncd /synchronizer com.meego.msyncd.startSync string:caldav-sync-55
  */

class BUTEOCALDAVPLUGINSHARED_EXPORT CalDavClient : public Buteo::ClientPlugin
{
    Q_OBJECT

public:
    CalDavClient(const QString &aPluginName,
                 const Buteo::SyncProfile &aProfile,
                 Buteo::PluginCbInterface *aCbInterface);
    virtual ~CalDavClient();

    virtual bool init();
    virtual bool uninit();
    virtual bool startSync();
    virtual void abortSync(Sync::SyncStatus aStatus = Sync::SYNC_ABORTED);
    virtual Buteo::SyncResults getSyncResults() const;
    virtual bool cleanUp();

public Q_SLOTS:
    virtual void connectivityStateChanged(Sync::ConnectivityType aType, bool aState);

private Q_SLOTS:
    void start();
    void authenticationError();
    void notebookSyncFinished();

private:
    bool initConfig();
    void closeConfig();
    void syncFinished(Buteo::SyncResults::MinorCode minorErrorCode, const QString &message = QString());
    void clearAgents();
    void deleteNotebooksForAccount(int accountId, mKCal::ExtendedCalendar::Ptr calendar, mKCal::ExtendedStorage::Ptr storage);
    bool cleanSyncRequired();
    void getSyncDateRange(const QDateTime &sourceDate, QDateTime *fromDateTime, QDateTime *toDateTime);
    QList<Buteo::Dav::CalendarInfo> loadAccountCalendars() const;
    QList<Buteo::Dav::CalendarInfo> mergeAccountCalendars(const QList<Buteo::Dav::CalendarInfo> &calendars) const;
    void removeAccountCalendars(const QStringList &paths);
    void listCalendars(const QString &home = QString());
    void syncCalendars(const QList<Buteo::Dav::CalendarInfo> &allCalendarInfo);

    Buteo::SyncProfile::SyncDirection syncDirection();
    Buteo::SyncProfile::ConflictResolutionPolicy conflictResolutionPolicy();

    void setCredentialsNeedUpdate();

    mutable QScopedPointer<Sailfish::KeyProvider::ProcessMutex> mProcessMutex;
    QList<NotebookSyncAgent *> mNotebookSyncAgents;
    Accounts::Manager* mManager;
    QSharedPointer<Accounts::AccountService> mService;
    AuthHandler* mAuth;
    mKCal::ExtendedCalendar::Ptr mCalendar;
    mKCal::ExtendedStorage::Ptr mStorage;
    Buteo::SyncResults mResults;
    Sync::SyncStatus mSyncStatus;
    Buteo::SyncProfile::SyncDirection mSyncDirection;
    Buteo::SyncProfile::ConflictResolutionPolicy mConflictResPolicy;
    Buteo::Dav::Client* mDAV;

    friend class tst_CalDavClient;
};


class CalDavClientLoader : public Buteo::SyncPluginLoader
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.sailfishos.plugins.sync.CalDavClientLoader")
    Q_INTERFACES(Buteo::SyncPluginLoader)

public:
    /*! \brief Creates CalDav client plugin
     *
     * @param aPluginName Name of this client plugin
     * @param aProfile Profile to use
     * @param aCbInterface Pointer to the callback interface
     * @return Client plugin on success, otherwise NULL
     */
    Buteo::ClientPlugin* createClientPlugin(const QString& pluginName,
                                            const Buteo::SyncProfile& profile,
                                            Buteo::PluginCbInterface* cbInterface) override;
};

#endif // CALDAVCLIENT_H
