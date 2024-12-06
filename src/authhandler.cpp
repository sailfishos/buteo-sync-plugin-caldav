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

#include "authhandler.h"

#include <QVariantMap>
#include <QTextStream>
#include <QFile>
#include <QStringList>
#include <QDebug>
#include <QUrl>

#include <Accounts/Manager>
#include <Accounts/AuthData>
#include <Accounts/AccountService>
#include <SignOn/SessionData>

#include <signon-plugins/oauth2data.h>

#include <ProfileEngineDefs.h>
#include "logging.h"

#include <sailfishkeyprovider.h>

using namespace Accounts;
using namespace SignOn;

const QString RESPONSE_TYPE         ("ResponseType");
const QString SCOPE                 ("Scope");
const QString AUTH_PATH             ("AuthPath");
const QString TOKEN_PATH            ("TokenPath");
const QString REDIRECT_URI          ("RedirectUri");
const QString HOST                  ("Host");

AuthHandler::AuthHandler(QSharedPointer<Accounts::AccountService> service, QObject *parent)
    : QObject(parent)
    , mAccountService(service)
{
}

bool AuthHandler::init()
{
    FUNCTION_CALL_TRACE(lcCalDavTrace);

    if (mAccountService == NULL) {
        qCDebug(lcCalDav) << "Invalid account service";
        return false;
    }
    const Accounts::AuthData &auth = mAccountService->authData();
    if (auth.credentialsId() == 0) {
        qCWarning(lcCalDav) << "Cannot authenticate, no credentials stored for service:" << mAccountService->service().name();
        return false;
    }

    mIdentity = Identity::existingIdentity(auth.credentialsId(), this);
    if (!mIdentity) {
        qCWarning(lcCalDav) << "Cannot get existing identity for credentials:" << auth.credentialsId();
        return false;
    }

    mSession = mIdentity->createSession(auth.method().toLatin1());
    if (!mSession) {
        qCDebug(lcCalDav) << "Signon session could not be created with method" << auth.method();
        return false;
    }

    connect(mSession, SIGNAL(response(const SignOn::SessionData &)),
            this, SLOT(sessionResponse(const SignOn::SessionData &)));

    connect(mSession, SIGNAL(error(const SignOn::Error &)),
            this, SLOT(error(const SignOn::Error &)));

    return true;
}

void AuthHandler::sessionResponse(const SessionData &sessionData)
{
    FUNCTION_CALL_TRACE(lcCalDavTrace);

    if (mSession->name().compare("password", Qt::CaseInsensitive) == 0) {
        mUsername = sessionData.UserName();
        mPassword = sessionData.Secret();
    } else if (mSession->name().compare("oauth2", Qt::CaseInsensitive) == 0) {
        OAuth2PluginNS::OAuth2PluginTokenData response = sessionData.data<OAuth2PluginNS::OAuth2PluginTokenData>();
        mToken = response.AccessToken();
    } else {
        qCCritical(lcCalDav) << "Unsupported Mechanism requested!";
        emit failed();
        return;
    }
    qCDebug(lcCalDav) << "Authenticated!";
    emit success();
}

const QString AuthHandler::token()
{
    return mToken;
}

const QString AuthHandler::username()
{
    return mUsername;
}

const QString AuthHandler::password()
{
    return mPassword;
}

void AuthHandler::authenticate()
{
    FUNCTION_CALL_TRACE(lcCalDavTrace);

    const Accounts::AuthData &auth = mAccountService->authData();

    if (mSession->name().compare("password", Qt::CaseInsensitive) == 0) {
        SignOn::SessionData data(auth.parameters());
        data.setUiPolicy(SignOn::NoUserInteractionPolicy);
        mSession->process(data, auth.mechanism());
    } else if (mSession->name().compare("oauth2", Qt::CaseInsensitive) == 0) {
        const QByteArray providerName = mAccountService->account()->providerName().toLatin1();
        const QString clientId = storedKeyValue(providerName.constData(), "caldav", "client_id");
        const QString clientSecret = storedKeyValue(providerName.constData(), "caldav", "client_secret");
        OAuth2PluginNS::OAuth2PluginData data;
        data.setClientId(clientId);
        data.setClientSecret(clientSecret);
        data.setHost(auth.parameters().value(HOST).toString());
        data.setAuthPath(auth.parameters().value(AUTH_PATH).toString());
        data.setTokenPath(auth.parameters().value(TOKEN_PATH).toString());
        data.setRedirectUri(auth.parameters().value(REDIRECT_URI).toString());
        data.setResponseType(QStringList() << auth.parameters().value(RESPONSE_TYPE).toString());
        data.setScope(auth.parameters().value(SCOPE).toStringList());

        mSession->process(data, auth.mechanism());
    } else {
        qCCritical(lcCalDav) << "Unsupported Method requested!";
        emit failed();
    }
}

void AuthHandler::error(const SignOn::Error & error)
{
    FUNCTION_CALL_TRACE(lcCalDavTrace);
    qCDebug(lcCalDav) << "Auth error:" << error.message();
    emit failed();
}

QString AuthHandler::storedKeyValue(const char *provider, const char *service, const char *keyName)
{
    FUNCTION_CALL_TRACE(lcCalDavTrace);

    char *storedKey = NULL;
    QString retn;

    int success = SailfishKeyProvider_storedKey(provider, service, keyName, &storedKey);
    if (success == 0 && storedKey != NULL && strlen(storedKey) != 0) {
        retn = QLatin1String(storedKey);
    }

    if (storedKey) {
        free(storedKey);
    }

    return retn;
}
