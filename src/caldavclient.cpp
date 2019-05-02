/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2013 Jolla Ltd. and/or its subsidiary(-ies).
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

static const char * const cleanSyncMarkersFile = "/home/nemo/.local/share/system/privileged/Sync/caldav.ini";
static const char * const cleanSyncMarkersFileDir = "/home/nemo/.local/share/system/privileged/Sync/";

static const char * const SYNC_PREV_PERIOD_KEY = "Sync Previous Months Span";
static const char * const SYNC_NEXT_PERIOD_KEY = "Sync Next Months Span";

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
    , mSyncAborted(false)
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
    FUNCTION_CALL_TRACE;
    abort(aStatus);
}

void CalDavClient::abort(Sync::SyncStatus status)
{
    FUNCTION_CALL_TRACE;
    mSyncAborted = true;
    syncFinished(status, QStringLiteral("Sync aborted"));
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
        mKCal::Notebook::List notebookList = storage->notebooks();
        LOG_DEBUG("Total Number of Notebooks in device = " << notebookList.count());
        int deletedCount = 0;
        Q_FOREACH (mKCal::Notebook::Ptr notebook, notebookList) {
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
    // multiple CalDavClient processes might be spawned (e.g. sync with different accounts)
    // so use a process mutex to ensure that only one will access the clean sync marker file at any time.
    if (!mProcessMutex) {
        mProcessMutex.reset(new Sailfish::KeyProvider::ProcessMutex(cleanSyncMarkersFile));
    }
    mProcessMutex->lock();

    char *cleaned_value = SailfishKeyProvider_ini_read(
            cleanSyncMarkersFile,
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
        mKCal::Notebook::List allNotebooks = mStorage->notebooks();
        Q_FOREACH (mKCal::Notebook::Ptr nb, allNotebooks) {
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
        Q_FOREACH (int notebookAccountId, notebookAccountIds) {
            if (!accountIdList.contains(notebookAccountId)) {
                LOG_WARNING("purging notebooks for deleted caldav account" << notebookAccountId);
                deleteNotebooksForAccount(notebookAccountId, mCalendar, mStorage);
            }
        }

        // mark this account as having been cleaned.
        if (SailfishKeyProvider_ini_write(
                cleanSyncMarkersFileDir,
                cleanSyncMarkersFile,
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

static Accounts::Account* getAccountForCalendars(Accounts::Manager *manager,
                                                 int accountId,
                                                 Accounts::Service *service)
{
    Accounts::Account *account = manager->account(accountId);
    if (!account) {
        LOG_WARNING("cannot find account" << accountId);
        return NULL;
    }
    if (!account->enabled()) {
        LOG_WARNING("Account" << accountId << "is disabled!");
        return NULL;
    }
    Q_FOREACH (const Accounts::Service &currService, account->services()) {
        account->selectService(currService);
        if (!account->value("calendars").toStringList().isEmpty()) {
            *service = currService;
            break;
        }
    }
    if (!service->isValid()) {
        LOG_WARNING("cannot find a service for account" << accountId << "with a valid calendar list");
        return NULL;
    }

    account->selectService(*service);
    if (!account->enabled()) {
        LOG_WARNING("Account" << accountId << "service:" << service->name() << "is disabled!");
        return NULL;
    }

    return account;
}

QList<Settings::CalendarInfo> CalDavClient::loadCalendars(Accounts::Account *account, Accounts::Service srv) const
{
    if (!account || !srv.isValid()) {
        return QList<Settings::CalendarInfo>();
    }
    account->selectService(srv);
    QStringList calendarPaths = account->value("calendars").toStringList();
    QStringList enabledCalendars = account->value("enabled_calendars").toStringList();
    QStringList displayNames = account->value("calendar_display_names").toStringList();
    QStringList colors = account->value("calendar_colors").toStringList();
    account->selectService(Accounts::Service());

    if (enabledCalendars.count() > calendarPaths.count()
            || calendarPaths.count() != displayNames.count()
            || calendarPaths.count() != colors.count()) {
        LOG_WARNING("Bad calendar data for account" << account->id() << "and service" << srv.name());
        return QList<Settings::CalendarInfo>();
    }
    QList<Settings::CalendarInfo> allCalendarInfo;
    for (int i=0; i<calendarPaths.count(); i++) {
        if (!enabledCalendars.contains(calendarPaths[i])) {
            continue;
        }
        // the calendar path may be percent-encoded.  Return UTF-8 QString.
        QString remoteCalendarPath = QUrl::fromPercentEncoding(calendarPaths[i].toUtf8());
        if (mSettings.serverAddress().contains(QStringLiteral("caldav.calendar.yahoo.com"))) {
            // Yahoo! seems to double-percent-encode for some reason
            remoteCalendarPath = QUrl::fromPercentEncoding(remoteCalendarPath.toUtf8());
        }
        Settings::CalendarInfo info = { remoteCalendarPath, displayNames[i], colors[i] };
        allCalendarInfo << info;
    }
    return allCalendarInfo;
}

void CalDavClient::mergeCalendars(const QList<Settings::CalendarInfo> &calendars)
{
    Accounts::Service srv;
    Accounts::Account *account = getAccountForCalendars(mManager, mAccountId, &srv);
    if (!account) {
        return;
    }

    QStringList calendarPaths = account->value("calendars").toStringList();
    QStringList enabledCalendars = account->value("enabled_calendars").toStringList();
    QStringList displayNames = account->value("calendar_display_names").toStringList();
    QStringList colors = account->value("calendar_colors").toStringList();
    account->selectService(Accounts::Service());

    bool modified = false;
    for (QList<Settings::CalendarInfo>::ConstIterator it = calendars.constBegin();
         it != calendars.constEnd(); ++it) {
        if (!calendarPaths.contains(it->remotePath)) {
            LOG_DEBUG("Found a new upstream calendar:" << it->remotePath << it->displayName);
            calendarPaths.append(it->remotePath);
            enabledCalendars.append(it->remotePath);
            displayNames.append(it->displayName);
            colors.append(it->color);
            modified = true;
        } else {
            int i = calendarPaths.indexOf(it->remotePath);
            LOG_DEBUG("Already existing calendar:" << it->remotePath << displayNames[i] << colors[i]);
            if (displayNames[i] != it->displayName
                || colors[i] != it->color) {
                LOG_DEBUG("Updating display name and color:" << it->displayName << it->color);
                displayNames[i] = it->displayName;
                colors[i] = it->color;
                modified = true;
            }
        }
    }
    if (modified) {
        account->selectService(srv);
        account->setValue("calendars", calendarPaths);
        account->setValue("enabled_calendars", enabledCalendars);
        account->setValue("calendar_display_names", displayNames);
        account->setValue("calendar_colors", colors);
        account->selectService(Accounts::Service());
        account->syncAndBlock();
        mSettings.setCalendars(loadCalendars(account, srv));
    }
}

void CalDavClient::removeCalendars(const QStringList &paths)
{
    Accounts::Service srv;
    Accounts::Account *account = getAccountForCalendars(mManager, mAccountId, &srv);
    if (!account) {
        return;
    }

    QStringList calendarPaths = account->value("calendars").toStringList();
    QStringList enabledCalendars = account->value("enabled_calendars").toStringList();
    QStringList displayNames = account->value("calendar_display_names").toStringList();
    QStringList colors = account->value("calendar_colors").toStringList();
    account->selectService(Accounts::Service());

    bool modified = false;
    for (QStringList::ConstIterator it = paths.constBegin();
         it != paths.constEnd(); ++it) {
        int at = calendarPaths.indexOf(*it);
        if (at >= 0) {
            LOG_DEBUG("Found a deleted upstream calendar:" << *it << displayNames[at]);
            calendarPaths.removeAt(at);
            enabledCalendars.removeAll(*it);
            displayNames.removeAt(at);
            colors.removeAt(at);
            modified = true;
        }
    }
    if (modified) {
        account->selectService(srv);
        account->setValue("calendars", calendarPaths);
        account->setValue("enabled_calendars", enabledCalendars);
        account->setValue("calendar_display_names", displayNames);
        account->setValue("calendar_colors", colors);
        account->selectService(Accounts::Service());
        account->syncAndBlock();
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
    Accounts::Account *account = getAccountForCalendars(mManager, accountId, &srv);
    if (!account) {
        return false;
    }

    mSettings.setServerAddress(account->value("server_address").toString());
    if (mSettings.serverAddress().isEmpty()) {
        LOG_WARNING("remote_address not found in service settings");
        return false;
    }
    mSettings.setIgnoreSSLErrors(account->value("ignore_ssl_errors").toBool());
    mSettings.setCalendars(loadCalendars(account, srv));
    if (mSettings.calendars().isEmpty()) {
        LOG_WARNING("no calendars found");
        return false;
    }
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

void CalDavClient::syncFinished(int minorErrorCode, const QString &message)
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
        mResults = Buteo::SyncResults(QDateTime::currentDateTime().toUTC(),
                                      Buteo::SyncResults::SYNC_RESULT_SUCCESS,
                                      Buteo::SyncResults::NO_ERROR);
        emit success(getProfileName(), message);
    } else {
        LOG_WARNING("CalDAV sync failed:" << minorErrorCode << message);
        mResults = Buteo::SyncResults(QDateTime::currentDateTime().toUTC(),
                                      Buteo::SyncResults::SYNC_RESULT_FAILED,
                                      minorErrorCode);

        if (minorErrorCode == Buteo::SyncResults::AUTHENTICATION_FAILURE) {
            setCredentialsNeedUpdate(mSettings.accountId());
        }

        emit error(getProfileName(), message, minorErrorCode);
    }
}

void CalDavClient::authenticationError()
{
    syncFinished(Buteo::SyncResults::AUTHENTICATION_FAILURE, QStringLiteral("Authentication failed"));
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

    QList<Settings::CalendarInfo> allCalendarInfo = mSettings.calendars();
    if (allCalendarInfo.isEmpty()) {
        syncFinished(Buteo::SyncResults::NO_ERROR, "No calendars for this account");
        return;
    }
    PropFind *calendarRequest = new PropFind(mNAManager, &mSettings, this);
    connect(calendarRequest, &Request::finished, this, &CalDavClient::syncCalendars);
    // Hacky here, try to guess the root for calendars from known
    // calendar paths, by removing one level.
    int lastIndex = allCalendarInfo[0].remotePath.lastIndexOf('/', -2);
    calendarRequest->listCalendars(allCalendarInfo[0].remotePath.left(lastIndex + 1));
}

void CalDavClient::syncCalendars()
{
    PropFind *request = qobject_cast<PropFind*>(sender());
    request->deleteLater();

    if (request->errorCode() == Buteo::SyncResults::NO_ERROR) {
        mergeCalendars(request->calendars());
    } else {
        LOG_WARNING("Unable to list calendars from server.");
    }

    QList<Settings::CalendarInfo> allCalendarInfo = mSettings.calendars();
    if (allCalendarInfo.isEmpty()) {
        syncFinished(Buteo::SyncResults::NO_ERROR, "No calendars for this account");
        return;
    }
    mCalendar = mKCal::ExtendedCalendar::Ptr(new mKCal::ExtendedCalendar(KDateTime::Spec::UTC()));
    mStorage = mKCal::ExtendedCalendar::defaultStorage(mCalendar);
    if (!mStorage || !mStorage->open()) {
        syncFinished(Buteo::SyncResults::DATABASE_FAILURE, "unable to open calendar storage");
        return;
    }

    cleanSyncRequired(mAccountId);

    QDateTime fromDateTime;
    QDateTime toDateTime;
    getSyncDateRange(QDateTime::currentDateTime().toUTC(), &fromDateTime, &toDateTime);

    // for each calendar path we need to sync:
    //  - if it is mapped to a known notebook, we need to perform quick sync
    //  - if no known notebook exists for it, we need to create one and perform clean sync
    Q_FOREACH (const Settings::CalendarInfo &calendarInfo, allCalendarInfo) {
        // TODO: could use some unused field from Notebook to store "need clean sync" flag?
        NotebookSyncAgent *agent = new NotebookSyncAgent
            (mCalendar, mStorage, mNAManager, &mSettings,
             calendarInfo.remotePath, this);
        if (!agent->setNotebookFromInfo(calendarInfo.displayName,
                                        calendarInfo.color,
                                        QString::number(mAccountId),
                                        getPluginName(),
                                        getProfileName())) {
            syncFinished(Buteo::SyncResults::DATABASE_FAILURE,
                         "unable to load calendar storage");
            return;
        }
        connect(agent, SIGNAL(finished(int,QString)),
                this, SLOT(notebookSyncFinished(int,QString)));
        mNotebookSyncAgents.append(agent);

        agent->startSync(fromDateTime, toDateTime);
    }
    if (mNotebookSyncAgents.isEmpty()) {
        syncFinished(Buteo::SyncResults::INTERNAL_ERROR, "Could not add or find existing notebooks for this account");
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

void CalDavClient::notebookSyncFinished(int errorCode, const QString &errorString)
{
    FUNCTION_CALL_TRACE;
    LOG_INFO("Notebook sync finished. Total agents:" << mNotebookSyncAgents.count());

    NotebookSyncAgent *agent = qobject_cast<NotebookSyncAgent*>(sender());
    agent->disconnect(this);

    // At that point, the finishing agent has sent all possible local additions /
    // modifications to upstream and upstream has returned according etags.
    // Even in case of error, some PUT may have succeeded and etags should
    // be saved accordingly.
    if (!mStorage->save()) {
        LOG_WARNING("Unable to save calendar storage after etag changes!");
        syncFinished(Buteo::SyncResults::DATABASE_FAILURE,
                     QStringLiteral("unable to save upstream etags"));
        return;
    }

    if (errorCode != Buteo::SyncResults::NO_ERROR) {
        LOG_WARNING("Aborting! Notebook synchronisation failed:" << errorCode << ":" << errorString);
        syncFinished(errorCode, errorString);
        return;
    }
    bool finished = true;
    for (int i=0; i<mNotebookSyncAgents.count(); i++) {
        if (!mNotebookSyncAgents[i]->isFinished()) {
            finished = false;
            break;
        }
    }
    if (finished && !mSyncAborted) {
        QStringList deletedNotebooks;
        for (int i=0; i<mNotebookSyncAgents.count(); i++) {
            if (!mNotebookSyncAgents[i]->applyRemoteChanges()) {
                LOG_WARNING("Unable to write notebook changes for notebook at index:" << i);
                syncFinished(Buteo::SyncResults::INTERNAL_ERROR, QStringLiteral("unable to write notebook changes"));
                return;
            }
            if (mNotebookSyncAgents[i]->isDeleted()) {
                deletedNotebooks += mNotebookSyncAgents[i]->path();
            }
            mNotebookSyncAgents[i]->finalize();
        }
        if (mStorage->save()) {
            removeCalendars(deletedNotebooks);
            LOG_DEBUG("Calendar storage saved successfully after writing notebook changes!");
            syncFinished(errorCode, errorString); // NO_ERROR, QString()
        } else {
            LOG_WARNING("Unable to save calendar storage after writing notebook changes!");
            syncFinished(Buteo::SyncResults::DATABASE_FAILURE, QStringLiteral("unable to save calendar storage"));
        }
    }
}

void CalDavClient::setCredentialsNeedUpdate(int accountId)
{
    Accounts::Account *account = mManager->account(accountId);
    if (account) {
        Q_FOREACH (const Accounts::Service &currService, account->services()) {
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
