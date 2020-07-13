/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (c) 2013 - 2020 Jolla Ltd. and/or its subsidiary(-ies).
 * Copyright (c) 2020 Open Mobile Platform LLC.
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

#include "caldavclient.h"
#include "propfind.h"
#include "notebooksyncagent.h"

#include <sailfishkeyprovider_iniparser.h>

#include <extendedcalendar.h>
#include <extendedstorage.h>
#include <notebook.h>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDateTime>
#include <QtGlobal>

#include <Accounts/Manager>
#include <Accounts/Account>

#include <PluginCbInterface.h>
#include <LogMacros.h>
#include <ProfileEngineDefs.h>
#include <ProfileManager.h>

namespace {

const QString cleanSyncMarkersFileDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
    + QStringLiteral("/system/privileged/Sync");
const QString cleanSyncMarkersFile = cleanSyncMarkersFileDir + QStringLiteral("/caldav.ini");

const char * const SYNC_PREV_PERIOD_KEY = "Sync Previous Months Span";
const char * const SYNC_NEXT_PERIOD_KEY = "Sync Next Months Span";

}

extern "C" CalDavClient* createPlugin(const QString& aPluginName,
                                         const Buteo::SyncProfile& aProfile,
                                         Buteo::PluginCbInterface *aCbInterface)
{
    return new CalDavClient(aPluginName, aProfile, aCbInterface);
}

extern "C" void destroyPlugin(CalDavClient *aClient)
{
    delete aClient;
}

CalDavClient::CalDavClient(const QString& aPluginName,
                            const Buteo::SyncProfile& aProfile,
                            Buteo::PluginCbInterface *aCbInterface)
    : ClientPlugin(aPluginName, aProfile, aCbInterface)
    , mManager(0)
    , mAuth(0)
    , mCalendar(0)
    , mStorage(0)
    , mAccountId(0)
{
    FUNCTION_CALL_TRACE;
}

CalDavClient::~CalDavClient()
{
    FUNCTION_CALL_TRACE;
}

bool CalDavClient::init()
{
    FUNCTION_CALL_TRACE;

    mNAManager = new QNetworkAccessManager(this);

    if (initConfig()) {
        return true;
    } else {
        // Uninitialize everything that was initialized before failure.
        uninit();
        return false;
    }
}

bool CalDavClient::uninit()
{
    FUNCTION_CALL_TRACE;
    return true;
}

bool CalDavClient::startSync()
{
    FUNCTION_CALL_TRACE;

    if (!mAuth)
        return false;

    mAuth->authenticate();

    LOG_DEBUG ("Init done. Continuing with sync");

    return true;
}

void CalDavClient::abortSync(Sync::SyncStatus aStatus)
{
    Q_UNUSED(aStatus);
    FUNCTION_CALL_TRACE;
    for (NotebookSyncAgent *agent: mNotebookSyncAgents) {
        disconnect(agent, &NotebookSyncAgent::finished,
                   this, &CalDavClient::notebookSyncFinished);
        agent->abort();
    }
    syncFinished(Buteo::SyncResults::ABORTED, QLatin1String("Sync aborted"));
}

bool CalDavClient::cleanUp()
{
    FUNCTION_CALL_TRACE;

    // This function is called after the account has been deleted to allow the plugin to remove
    // all the notebooks associated with the account.

    QString accountIdString = iProfile.key(Buteo::KEY_ACCOUNT_ID);
    int accountId = accountIdString.toInt();
    if (accountId == 0) {
        LOG_WARNING("profile does not specify" << Buteo::KEY_ACCOUNT_ID);
        return false;
    }

    mAccountId = accountId;
    mKCal::ExtendedCalendar::Ptr calendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
    mKCal::ExtendedStorage::Ptr storage = mKCal::ExtendedCalendar::defaultStorage(calendar);
    if (!storage->open()) {
        calendar->close();
        LOG_WARNING("unable to open calendar storage");
        return false;
    }

    deleteNotebooksForAccount(accountId, calendar, storage);
    storage->close();
    calendar->close();
    return true;
}

void CalDavClient::deleteNotebooksForAccount(int accountId, mKCal::ExtendedCalendar::Ptr, mKCal::ExtendedStorage::Ptr storage)
{
    FUNCTION_CALL_TRACE;

    if (storage) {
        QString notebookAccountPrefix = QString::number(accountId) + "-"; // for historical reasons!
        QString accountIdStr = QString::number(accountId);
        const mKCal::Notebook::List notebookList = storage->notebooks();
        LOG_DEBUG("Total Number of Notebooks in device = " << notebookList.count());
        int deletedCount = 0;
        for (mKCal::Notebook::Ptr notebook : notebookList) {
            if (notebook->account() == accountIdStr || notebook->account().startsWith(notebookAccountPrefix)) {
                if (storage->deleteNotebook(notebook)) {
                    deletedCount++;
                }
            }
        }
        LOG_DEBUG("Deleted" << deletedCount << "notebooks");
    }
}

bool CalDavClient::cleanSyncRequired(int accountId)
{
    static const QByteArray iniFileDir = cleanSyncMarkersFileDir.toUtf8();
    static const QByteArray iniFile = cleanSyncMarkersFile.toUtf8();

    // multiple CalDavClient processes might be spawned (e.g. sync with different accounts)
    // so use a process mutex to ensure that only one will access the clean sync marker file at any time.
    if (!mProcessMutex) {
        mProcessMutex.reset(new Sailfish::KeyProvider::ProcessMutex(iniFile.constData()));
    }
    mProcessMutex->lock();

    char *cleaned_value = SailfishKeyProvider_ini_read(
            iniFile.constData(),
            "General",
            QStringLiteral("%1-cleaned").arg(accountId).toLatin1());
    bool alreadyClean = cleaned_value != 0 && strncmp(cleaned_value, "true", 4) == 0;
    free(cleaned_value);

    if (!alreadyClean) {
        // first, delete any data associated with this account, so this sync will be a clean sync.
        LOG_WARNING("Deleting caldav notebooks associated with this account:" << accountId << "due to clean sync");
        deleteNotebooksForAccount(accountId, mCalendar, mStorage);
        // now delete notebooks for non-existent accounts.
        LOG_WARNING("Deleting caldav notebooks associated with nonexistent accounts due to clean sync");
        // a) find out which accounts are associated with each of our notebooks.
        QList<int> notebookAccountIds;
        const mKCal::Notebook::List allNotebooks = mStorage->notebooks();
        for (mKCal::Notebook::Ptr nb : allNotebooks) {
            QString nbAccount = nb->account();
            if (!nbAccount.isEmpty() && nb->pluginName().contains(QStringLiteral("caldav"))) {
                // caldav notebook->account() values used to be like: "55-/user/calendars/someCalendar"
                int indexOfHyphen = nbAccount.indexOf('-');
                if (indexOfHyphen > 0) {
                    // this is an old caldav notebook which used "accountId-remoteServerPath" form
                    nbAccount.chop(nbAccount.length() - indexOfHyphen);
                }
                bool ok = true;
                int notebookAccountId = nbAccount.toInt(&ok);
                if (!ok) {
                    LOG_WARNING("notebook account value was strange:" << nb->account() << "->" << nbAccount << "->" << "not ok");
                } else {
                    LOG_WARNING("found account id:" << notebookAccountId << "for" << nb->account() << "->" << nbAccount);
                    if (!notebookAccountIds.contains(notebookAccountId)) {
                        notebookAccountIds.append(notebookAccountId);
                    }
                }
            }
        }
        // b) find out if any of those accounts don't exist - if not,
        Accounts::AccountIdList accountIdList = mManager->accountList();
        for (int notebookAccountId : const_cast<const QList<int>&>(notebookAccountIds)) {
            if (!accountIdList.contains(notebookAccountId)) {
                LOG_WARNING("purging notebooks for deleted caldav account" << notebookAccountId);
                deleteNotebooksForAccount(notebookAccountId, mCalendar, mStorage);
            }
        }

        // mark this account as having been cleaned.
        if (SailfishKeyProvider_ini_write(
                iniFileDir.constData(),
                iniFile.constData(),
                "General",
                QStringLiteral("%1-cleaned").arg(accountId).toLatin1(),
                "true") != 0) {
            LOG_WARNING("Failed to mark account as clean!  Next sync will be unnecessarily cleaned also!");
        }

        // finished; return true because this will be a clean sync.
        LOG_WARNING("Finished pre-sync cleanup with caldav account" << accountId);
        mProcessMutex->unlock();
        return true;
    }

    mProcessMutex->unlock();
    return false;
}

void CalDavClient::connectivityStateChanged(Sync::ConnectivityType aType, bool aState)
{
    FUNCTION_CALL_TRACE;
    LOG_DEBUG("Received connectivity change event:" << aType << " changed to " << aState);
    if (aType == Sync::CONNECTIVITY_INTERNET && !aState) {
        // we lost connectivity during sync.
        abortSync(Sync::SYNC_CONNECTION_ERROR);
    }
}

Accounts::Account* CalDavClient::getAccountForCalendars(Accounts::Service *service) const
{
    Accounts::Account *account = mManager->account(mAccountId);
    if (!account) {
        LOG_WARNING("cannot find account" << mAccountId);
        return NULL;
    }
    if (!account->enabled()) {
        LOG_WARNING("Account" << mAccountId << "is disabled!");
        return NULL;
    }
    Accounts::Service calendarService;
    const Accounts::ServiceList caldavServices = account->services("caldav");
    for (const Accounts::Service &currService : caldavServices) {
        account->selectService(currService);
        if (account->value("caldav-sync/profile_id").toString() == getProfileName()) {
            calendarService = currService;
            break;
        }
    }
    if (!calendarService.isValid()) {
        LOG_WARNING("cannot find a service for account" << mAccountId << "with a valid calendar list");
        return NULL;
    }

    account->selectService(calendarService);
    if (!account->enabled()) {
        LOG_WARNING("Account" << mAccountId << "service:" << service->name() << "is disabled!");
        return NULL;
    }

    if (service) {
        *service = calendarService;
    }
    return account;
}

struct CalendarSettings
{
public:
    CalendarSettings(Accounts::Account *account)
        : paths(account->value("calendars").toStringList())
        , displayNames(account->value("calendar_display_names").toStringList())
        , colors(account->value("calendar_colors").toStringList())
        , enabled(account->value("enabled_calendars").toStringList())
    {
        if (enabled.count() > paths.count()
            || paths.count() != displayNames.count()
            || paths.count() != colors.count()) {
            LOG_WARNING("Bad calendar data for account" << account->id());
            paths.clear();
            displayNames.clear();
            colors.clear();
            enabled.clear();
        }
    };
    // Constructs a list of CalendarInfo from value stored in settings.
    QList<PropFind::CalendarInfo> toCalendars()
    {
        QList<PropFind::CalendarInfo> allCalendarInfo;
        for (int i = 0; i < paths.count(); i++) {
            allCalendarInfo << PropFind::CalendarInfo(paths[i],
                    displayNames[i], colors[i]);
        }
        return allCalendarInfo;
    };
    QList<PropFind::CalendarInfo> enabledCalendars(const QList<PropFind::CalendarInfo> &calendars)
    {
        QList<PropFind::CalendarInfo> filteredCalendarInfo;
        for (const PropFind::CalendarInfo &info : calendars) {
            if (!enabled.contains(info.remotePath)) {
                continue;
            }
            filteredCalendarInfo << info;
        }
        return filteredCalendarInfo;
    };
    void add(const PropFind::CalendarInfo &infos)
    {
        paths.append(infos.remotePath);
        enabled.append(infos.remotePath);
        displayNames.append(infos.displayName);
        colors.append(infos.color);
    };
    bool update(const PropFind::CalendarInfo &infos, bool &modified)
    {
        int i = paths.indexOf(infos.remotePath);
        if (i < 0) {
            return false;
        }
        if (displayNames[i] != infos.displayName || colors[i] != infos.color) {
            displayNames[i] = infos.displayName;
            colors[i] = infos.color;
            modified = true;
        }
        return true;
    };
    bool remove(const QString &path)
    {
        int at = paths.indexOf(path);
        if (at >= 0) {
            paths.removeAt(at);
            enabled.removeAll(path);
            displayNames.removeAt(at);
            colors.removeAt(at);
        }
        return (at >= 0);
    }
    void store(Accounts::Account *account, const Accounts::Service &srv)
    {
        account->selectService(srv);
        account->setValue("calendars", paths);
        account->setValue("enabled_calendars", enabled);
        account->setValue("calendar_display_names", displayNames);
        account->setValue("calendar_colors", colors);
        account->selectService(Accounts::Service());
        account->syncAndBlock();
    };
private:
    QStringList paths;
    QStringList displayNames;
    QStringList colors;
    QStringList enabled;
};

QList<PropFind::CalendarInfo> CalDavClient::loadAccountCalendars() const
{
    Accounts::Service srv;
    Accounts::Account *account = getAccountForCalendars(&srv);
    if (!account) {
        return QList<PropFind::CalendarInfo>();
    }
    struct CalendarSettings calendarSettings(account);
    account->selectService(Accounts::Service());

    return calendarSettings.enabledCalendars(calendarSettings.toCalendars());
}

QList<PropFind::CalendarInfo> CalDavClient::mergeAccountCalendars(const QList<PropFind::CalendarInfo> &calendars) const
{
    Accounts::Service srv;
    Accounts::Account *account = getAccountForCalendars(&srv);
    if (!account) {
        return QList<PropFind::CalendarInfo>();
    }
    struct CalendarSettings calendarSettings(account);
    account->selectService(Accounts::Service());

    bool modified = false;
    for (QList<PropFind::CalendarInfo>::ConstIterator it = calendars.constBegin();
         it != calendars.constEnd(); ++it) {
        if (!calendarSettings.update(*it, modified)) {
            LOG_DEBUG("Found a new upstream calendar:" << it->remotePath << it->displayName);
            calendarSettings.add(*it);
            modified = true;
        } else {
            LOG_DEBUG("Already existing calendar:" << it->remotePath << it->displayName << it->color);
        }
    }
    if (modified) {
        LOG_DEBUG("Store modifications to calendar settings.");
        calendarSettings.store(account, srv);
    }

    return calendarSettings.enabledCalendars(calendars);
}

void CalDavClient::removeAccountCalendars(const QStringList &paths)
{
    Accounts::Service srv;
    Accounts::Account *account = getAccountForCalendars(&srv);
    if (!account) {
        return;
    }
    struct CalendarSettings calendarSettings(account);
    account->selectService(Accounts::Service());

    bool modified = false;
    for (QStringList::ConstIterator it = paths.constBegin();
         it != paths.constEnd(); ++it) {
        if (calendarSettings.remove(*it)) {
            LOG_DEBUG("Found a deleted upstream calendar:" << *it);
            modified = true;
        }
    }
    if (modified) {
        calendarSettings.store(account, srv);
    }
}

bool CalDavClient::initConfig()
{
    FUNCTION_CALL_TRACE;
    LOG_DEBUG("Initiating config...");

    if (!mManager) {
        mManager = new Accounts::Manager(this);
    }

    QString accountIdString = iProfile.key(Buteo::KEY_ACCOUNT_ID);
    bool accountIdOk = false;
    int accountId = accountIdString.toInt(&accountIdOk);
    if (!accountIdOk) {
        LOG_WARNING("no account id specified," << Buteo::KEY_ACCOUNT_ID << "not found in profile");
        return false;
    }
    mAccountId = accountId;

    Accounts::Service srv;
    Accounts::Account *account = getAccountForCalendars(&srv);
    if (!account) {
        return false;
    }

    mSettings.setServerAddress(account->value("server_address").toString());
    if (mSettings.serverAddress().isEmpty()) {
        LOG_WARNING("remote_address not found in service settings");
        return false;
    }
    mSettings.setIgnoreSSLErrors(account->value("ignore_ssl_errors").toBool());
    account->selectService(Accounts::Service());

    mAuth = new AuthHandler(mManager, accountId, srv.name());
    if (!mAuth->init()) {
        return false;
    }
    connect(mAuth, SIGNAL(success()), this, SLOT(start()));
    connect(mAuth, SIGNAL(failed()), this, SLOT(authenticationError()));

    mSettings.setAccountId(accountId);

    mSyncDirection = iProfile.syncDirection();
    mConflictResPolicy = iProfile.conflictResolutionPolicy();

    return true;
}

void CalDavClient::syncFinished(Buteo::SyncResults::MinorCode minorErrorCode,
                                const QString &message)
{
    FUNCTION_CALL_TRACE;

    clearAgents();

    if (mCalendar) {
        mCalendar->close();
    }
    if (mStorage) {
        mStorage->close();
        mStorage.clear();
    }

    if (minorErrorCode == Buteo::SyncResults::NO_ERROR) {
        LOG_DEBUG("CalDAV sync succeeded!" << message);
        mResults.setMajorCode(Buteo::SyncResults::SYNC_RESULT_SUCCESS);
        mResults.setMinorCode(Buteo::SyncResults::NO_ERROR);
        emit success(getProfileName(), message);
    } else {
        LOG_WARNING("CalDAV sync failed:" << minorErrorCode << message);
        mResults.setMajorCode(minorErrorCode == Buteo::SyncResults::ABORTED
                              ? Buteo::SyncResults::SYNC_RESULT_CANCELLED
                              : Buteo::SyncResults::SYNC_RESULT_FAILED);
        mResults.setMinorCode(minorErrorCode);

        if (minorErrorCode == Buteo::SyncResults::AUTHENTICATION_FAILURE) {
            setCredentialsNeedUpdate(mSettings.accountId());
        }

        emit error(getProfileName(), message, minorErrorCode);
    }
}

void CalDavClient::authenticationError()
{
    syncFinished(Buteo::SyncResults::AUTHENTICATION_FAILURE,
                 QLatin1String("Authentication failed"));
}

Buteo::SyncProfile::SyncDirection CalDavClient::syncDirection()
{
    FUNCTION_CALL_TRACE;
    return mSyncDirection;
}

Buteo::SyncProfile::ConflictResolutionPolicy CalDavClient::conflictResolutionPolicy()
{
    FUNCTION_CALL_TRACE;
    return mConflictResPolicy;
}

Buteo::SyncResults CalDavClient::getSyncResults() const
{
    FUNCTION_CALL_TRACE;

    return mResults;
}

void CalDavClient::getSyncDateRange(const QDateTime &sourceDate, QDateTime *fromDateTime, QDateTime *toDateTime)
{
    if (!fromDateTime || !toDateTime) {
        LOG_WARNING("fromDate or toDate is invalid");
        return;
    }
    const Buteo::Profile* client = iProfile.clientProfile();
    bool valid = (client != 0);
    uint prevPeriod = (valid) ? client->key(SYNC_PREV_PERIOD_KEY).toUInt(&valid) : 0;
    *fromDateTime = sourceDate.addMonths((valid) ? -int(qMin(prevPeriod, uint(120))) : -6);
    uint nextPeriod = (valid) ? client->key(SYNC_NEXT_PERIOD_KEY).toUInt(&valid) : 0;
    *toDateTime = sourceDate.addMonths((valid) ? int(qMin(nextPeriod, uint(120))) : 12);
}

void CalDavClient::start()
{
    FUNCTION_CALL_TRACE;

    if (!mAuth->username().isEmpty() && !mAuth->password().isEmpty()) {
        mSettings.setUsername(mAuth->username());
        mSettings.setPassword(mAuth->password());
    }
    mSettings.setAuthToken(mAuth->token());

    // read the calendar user address set, to get their mailto href.
    PropFind *userAddressSetRequest = new PropFind(mNAManager, &mSettings, this);
    connect(userAddressSetRequest, &Request::finished, [this, userAddressSetRequest] {
        const QString userPrincipal = userAddressSetRequest->userPrincipal();
        userAddressSetRequest->deleteLater();
        if (!userPrincipal.isEmpty()) {
            // determine the mailto href for this user.
            mSettings.setUserPrincipal(userPrincipal);
            PropFind *userHrefsRequest = new PropFind(mNAManager, &mSettings, this);
            connect(userHrefsRequest, &Request::finished, [this, userHrefsRequest] {
                userHrefsRequest->deleteLater();
                mSettings.setUserMailtoHref(userHrefsRequest->userMailtoHref());
                listCalendars(userHrefsRequest->userHomeHref());
            });
            userHrefsRequest->listUserAddressSet(userPrincipal);
        } else {
            // just continue normal calendar sync.
            listCalendars();
        }
    });
    userAddressSetRequest->listCurrentUserPrincipal();
}

void CalDavClient::listCalendars(const QString &home)
{
    QString remoteHome(home);
    if (remoteHome.isEmpty()) {
        LOG_WARNING("Cannot find the calendar root for this user, guess it from account.");
        Accounts::Service srv;
        Accounts::Account *account = getAccountForCalendars(&srv);
        if (!account) {
            syncFinished(Buteo::SyncResults::INTERNAL_ERROR,
                         QLatin1String("unable to find account for calendar detection"));
            return;
        }
        struct CalendarSettings calendarSettings(account);
        QList<PropFind::CalendarInfo> allCalendarInfo = calendarSettings.toCalendars();
        if (allCalendarInfo.isEmpty()) {
            syncFinished(Buteo::SyncResults::INTERNAL_ERROR,
                         QLatin1String("no calendar listed for detection"));
            return;
        }
        // Hacky here, try to guess the root for calendars from known
        // calendar paths, by removing one level.
        int lastIndex = allCalendarInfo[0].remotePath.lastIndexOf('/', -2);
        remoteHome = allCalendarInfo[0].remotePath.left(lastIndex + 1);
    }
    PropFind *calendarRequest = new PropFind(mNAManager, &mSettings, this);
    connect(calendarRequest, &Request::finished, this, [this, calendarRequest] {
        calendarRequest->deleteLater();
        if (calendarRequest->errorCode() == Buteo::SyncResults::NO_ERROR) {
            syncCalendars(mergeAccountCalendars(calendarRequest->calendars()));
        } else {
            LOG_WARNING("Cannot list calendars, fallback to stored ones in account.");
            syncCalendars(loadAccountCalendars());
        }
    });
    calendarRequest->listCalendars(remoteHome);
}

void CalDavClient::syncCalendars(const QList<PropFind::CalendarInfo> &allCalendarInfo)
{
    if (allCalendarInfo.isEmpty()) {
        syncFinished(Buteo::SyncResults::NO_ERROR,
                     QLatin1String("No calendars for this account"));
        return;
    }
    mCalendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
    mStorage = mKCal::ExtendedCalendar::defaultStorage(mCalendar);
    if (!mStorage || !mStorage->open()) {
        syncFinished(Buteo::SyncResults::DATABASE_FAILURE,
                     QLatin1String("unable to open calendar storage"));
        return;
    }

    cleanSyncRequired(mAccountId);

    QDateTime fromDateTime;
    QDateTime toDateTime;
    getSyncDateRange(QDateTime::currentDateTime().toUTC(), &fromDateTime, &toDateTime);

    // for each calendar path we need to sync:
    //  - if it is mapped to a known notebook, we need to perform quick sync
    //  - if no known notebook exists for it, we need to create one and perform clean sync
    for (const PropFind::CalendarInfo &calendarInfo : allCalendarInfo) {
        // TODO: could use some unused field from Notebook to store "need clean sync" flag?
        NotebookSyncAgent *agent = new NotebookSyncAgent
            (mCalendar, mStorage, mNAManager, &mSettings,
             calendarInfo.remotePath, calendarInfo.readOnly, this);
        const QString &email = (calendarInfo.userPrincipal == mSettings.userPrincipal()
                                || calendarInfo.userPrincipal.isEmpty())
            ? mSettings.userMailtoHref() : QString();
        if (!agent->setNotebookFromInfo(calendarInfo.displayName,
                                        calendarInfo.color,
                                        email,
                                        QString::number(mAccountId),
                                        getPluginName(),
                                        getProfileName())) {
            syncFinished(Buteo::SyncResults::DATABASE_FAILURE,
                         QLatin1String("unable to load calendar storage"));
            return;
        }
        connect(agent, &NotebookSyncAgent::finished,
                this, &CalDavClient::notebookSyncFinished);
        mNotebookSyncAgents.append(agent);

        agent->startSync(fromDateTime, toDateTime,
                         mSyncDirection != Buteo::SyncProfile::SYNC_DIRECTION_FROM_REMOTE,
                         mSyncDirection != Buteo::SyncProfile::SYNC_DIRECTION_TO_REMOTE);
    }
    if (mNotebookSyncAgents.isEmpty()) {
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR,
                     QLatin1String("Could not add or find existing notebooks for this account"));
    }
}

void CalDavClient::clearAgents()
{
    FUNCTION_CALL_TRACE;

    for (int i=0; i<mNotebookSyncAgents.count(); i++) {
        mNotebookSyncAgents[i]->deleteLater();
    }
    mNotebookSyncAgents.clear();
}

void CalDavClient::notebookSyncFinished()
{
    FUNCTION_CALL_TRACE;
    LOG_INFO("Notebook sync finished. Total agents:" << mNotebookSyncAgents.count());

    NotebookSyncAgent *agent = qobject_cast<NotebookSyncAgent*>(sender());
    if (!agent) {
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR,
                     QLatin1String("cannot get NotebookSyncAgent object"));
        return;
    }
    agent->disconnect(this);

    bool finished = true;
    for (int i=0; i<mNotebookSyncAgents.count(); i++) {
        if (!mNotebookSyncAgents[i]->isFinished()) {
            finished = false;
            break;
        }
    }
    if (finished) {
        bool hasDatabaseErrors = false;
        bool hasDownloadErrors = false;
        bool hasUploadErrors = false;
        QStringList deletedNotebooks;
        for (int i=0; i<mNotebookSyncAgents.count(); i++) {
            hasDownloadErrors = hasDownloadErrors || mNotebookSyncAgents[i]->hasDownloadErrors();
            hasUploadErrors = hasUploadErrors || mNotebookSyncAgents[i]->hasUploadErrors();
            if (!mNotebookSyncAgents[i]->applyRemoteChanges()) {
                LOG_WARNING("Unable to write notebook changes for notebook at index:" << i);
                hasDatabaseErrors = true;
            } else if (!mNotebookSyncAgents[i]->result().targetName().isEmpty()) {
                mResults.addTargetResults(mNotebookSyncAgents[i]->result());
            }
            if (mNotebookSyncAgents[i]->isDeleted()) {
                deletedNotebooks += mNotebookSyncAgents[i]->path();
            }
            mNotebookSyncAgents[i]->finalize();
        }
        removeAccountCalendars(deletedNotebooks);
        if (hasDownloadErrors) {
            mResults = Buteo::SyncResults();
            syncFinished(Buteo::SyncResults::CONNECTION_ERROR,
                         QLatin1String("unable to fetch all upstream changes"));
        } else if (hasUploadErrors) {
            mResults = Buteo::SyncResults();
            syncFinished(Buteo::SyncResults::CONNECTION_ERROR,
                         QLatin1String("unable to upsync all local changes"));
        } else if (hasDatabaseErrors) {
            mResults = Buteo::SyncResults();
            syncFinished(Buteo::SyncResults::DATABASE_FAILURE,
                         QLatin1String("unable to apply all remote changes"));
        } else {
            LOG_DEBUG("Calendar storage saved successfully after writing notebook changes!");
            syncFinished(Buteo::SyncResults::NO_ERROR);
        }
    }
}

void CalDavClient::setCredentialsNeedUpdate(int accountId)
{
    Accounts::Account *account = mManager->account(accountId);
    if (account) {
        const Accounts::ServiceList services = account->services();
        for (const Accounts::Service &currService : services) {
            account->selectService(currService);
            if (!account->value("calendars").toStringList().isEmpty()) {
                account->setValue(QStringLiteral("CredentialsNeedUpdate"), QVariant::fromValue<bool>(true));
                account->setValue(QStringLiteral("CredentialsNeedUpdateFrom"), QVariant::fromValue<QString>(QString::fromLatin1("caldav-sync")));
                account->selectService(Accounts::Service());
                account->syncAndBlock();
                break;
            }
        }
    }
}
