/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2013 Jolla Ltd. and/or its subsidiary(-ies).
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

#include "request_p.h"
#include "logging_p.h"

Request::Request(QNetworkAccessManager *manager,
                 Settings *settings,
                 const QString &requestType,
                 QObject *parent)
    : QObject(parent)
    , mNAManager(manager)
    , REQUEST_TYPE(requestType)
    , mSettings(settings)
    , mNetworkError(QNetworkReply::NoError)
{
    mSelfPointer = this;
}

bool Request::hasError() const
{
    return mErrorOccurred;
}

QString Request::errorMessage() const
{
    return mErrorMessage;
}

QByteArray Request::errorData() const
{
    return mErrorData;
}

QNetworkReply::NetworkError Request::networkError() const
{
    return mNetworkError;
}

QString Request::command() const
{
    return REQUEST_TYPE;
}

void Request::finishedWithReplyResult(const QString &uri, QNetworkReply *reply)
{
    mNetworkError = reply->error();
    if (reply->error() == QNetworkReply::NoError) {
        debugReplyAndReadAll(reply);
        finishedWithSuccess(uri);
    } else if (reply->error() == QNetworkReply::ContentOperationNotPermittedError) {
        // Gracefully continue when the operation fails for permission
        // reasons (like pushing to a read-only resource).
        qCDebug(lcDav) << "The" << command() << "operation requested on the remote content is not permitted";
        debugReplyAndReadAll(reply);
        finishedWithSuccess(uri);
    } else {
        int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qCWarning(lcDav) << "The" << command()
                         << "operation failed with error:" << reply->error()
                         << ", HTTP code:" << code;
        const QByteArray data(reply->readAll());
        debugReply(*reply, data);
        finishedWithError(uri,
                          QString("Network request failed with QNetworkReply::NetworkError: %1").arg(reply->error()),
                          data);
    }
}

void Request::slotSslErrors(QList<QSslError> errors)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    debugReplyAndReadAll(reply);

    if (mSettings->ignoreSSLErrors()) {
        qCDebug(lcDav) << "Ignoring SSL error response";
        reply->ignoreSslErrors(errors);
    } else {
        qCWarning(lcDav) << command() << "request received SSL error response!";
    }
}

void Request::requestFinished()
{
    if (wasDeleted()) {
        qCDebug(lcDav) << command() << "request was aborted";
        return;
    }

    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        finishedWithInternalError(QString());
        return;
    }
    reply->deleteLater();

    qCDebug(lcDav) << command() << "request finished:" << reply->error();

    handleReply(reply);
}

void Request::finishedWithError(const QString &uri, const QString &errorString, const QByteArray &errorData)
{
    mErrorOccurred = true;
    mErrorMessage = errorString;
    if (mErrorMessage.isEmpty()) {
        mErrorMessage = QString::fromLatin1("request %1 failure at %2").arg(command()).arg(uri);
    }
    mErrorData = errorData;
    emit finished(uri);
}

void Request::finishedWithInternalError(const QString &uri, const QString &errorString)
{
    finishedWithError(uri, errorString.isEmpty() ? QStringLiteral("Internal error") : errorString,
                      QByteArray());
}

void Request::finishedWithSuccess(const QString &uri)
{
    mErrorOccurred = false;
    emit finished(uri);
}

void Request::prepareRequest(QNetworkRequest *request, const QString &requestPath)
{
    QUrl url(mSettings->serverAddress());
    if (!mSettings->authToken().isEmpty()) {
        request->setRawHeader(QString("Authorization").toLatin1(),
                              QString("Bearer " + mSettings->authToken()).toLatin1());
    } else if (mSettings->serverAddress().endsWith(QStringLiteral(".yahoo.com"))
               || mSettings->serverAddress().endsWith(QStringLiteral(".icloud.com"))) {
        request->setRawHeader(QString("Authorization").toLatin1(),
                              QByteArray("Basic ") + QString::fromLatin1("%1:%2").arg(mSettings->username()).arg(mSettings->password()).toLatin1().toBase64());
    } else {
        url.setUserName(mSettings->username());
        url.setPassword(mSettings->password());
    }
    url.setPath(requestPath);
    request->setUrl(url);
}

bool Request::wasDeleted() const
{
    return mSelfPointer == 0;
}

void Request::debugRequest(const QNetworkRequest &request, const QByteArray &data)
{
    const QStringList lines = debuggingString(request, data).split('\n', QString::SkipEmptyParts);
    for (QString line : lines) {
        qCDebug(lcDav) << line.replace('\r', ' ');
    }
}

void Request::debugRequest(const QNetworkRequest &request, const QString &data)
{
    const QStringList lines = debuggingString(request, data.toUtf8()).split('\n', QString::SkipEmptyParts);
    for (QString line : lines) {
        qCDebug(lcDav) << line.replace('\r', ' ');
    }
}

void Request::debugReply(const QNetworkReply &reply, const QByteArray &data)
{
    const QStringList lines = debuggingString(reply, data).split('\n', QString::SkipEmptyParts);
    for (QString line : lines) {
        qCDebug(lcDav) << line.replace('\r', ' ');
    }
}

void Request::debugReplyAndReadAll(QNetworkReply *reply)
{
    const QStringList lines = debuggingString(*reply, reply->readAll()).split('\n', QString::SkipEmptyParts);
    for (QString line : lines) {
        qCDebug(lcDav) << line.replace('\r', ' ');
    }
}

QString Request::debuggingString(const QNetworkRequest &request, const QByteArray &data)
{
    QStringList text;
    text += "---------------------------------------------------------------------";
    const QList<QByteArray> &rawHeaderList = request.rawHeaderList();
    for (const QByteArray &rawHeader : rawHeaderList) {
        const QByteArray &rawHeaderData = request.rawHeader(rawHeader);
        if (rawHeader == "Authorization" && rawHeaderData.startsWith("Basic")) {
            text += rawHeader + " : " + "Basic user:password";
        } else {
            text += rawHeader + " : " + rawHeaderData;
        }
    }
    QUrl censoredUrl = request.url();
    censoredUrl.setUserName(QStringLiteral("user"));
    censoredUrl.setPassword(QStringLiteral("pass"));
    text += "URL = " + censoredUrl.toString();
    text += "Request : " + REQUEST_TYPE +  "\n" + data;
    text += "---------------------------------------------------------------------\n";
    return text.join(QChar('\n'));
}

QString Request::debuggingString(const QNetworkReply &reply, const QByteArray &data)
{
    QStringList text;
    text += "---------------------------------------------------------------------";
    text += REQUEST_TYPE + " response status code: " + reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toString();
    const QList<QNetworkReply::RawHeaderPair> headers = reply.rawHeaderPairs();
    text += REQUEST_TYPE + " response headers:";
    for (const QNetworkReply::RawHeaderPair header : headers) {
        text += "\t" + header.first + " : " + header.second;
    }
    if (!data.isEmpty()) {
        text += REQUEST_TYPE + " response data:" + data;
    }
    text += "---------------------------------------------------------------------\n";
    return text.join(QChar('\n'));
}
