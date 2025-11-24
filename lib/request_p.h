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

#ifndef REQUEST_H
#define REQUEST_H

#include "settings_p.h"

#include <QObject>
#include <QNetworkReply>
#include <QSslError>
#include <QNetworkRequest>

class Request : public QObject
{
    Q_OBJECT
public:
    explicit Request(QNetworkAccessManager *manager,
                     Settings *settings,
                     const QString &requestType,
                     QObject *parent = 0);

    QString command() const;
    bool hasError() const;
    QString errorMessage() const;
    QByteArray errorData() const;
    QNetworkReply::NetworkError networkError() const;

Q_SIGNALS:
    void finished(const QString &uri);

protected Q_SLOTS:
    virtual void slotSslErrors(QList<QSslError>);
    void requestFinished();

protected:
    void prepareRequest(QNetworkRequest *request, const QString &requestPath);
    virtual void handleReply(QNetworkReply *reply) = 0;

    bool wasDeleted() const;

    void finishedWithSuccess(const QString &uri);
    void finishedWithError(const QString &uri, const QString &errorMessage, const QByteArray &errorData);
    void finishedWithInternalError(const QString &uri, const QString &errorString = QString());
    void finishedWithReplyResult(const QString &uri, QNetworkReply *reply);

    void debugRequest(const QNetworkRequest &request, const QByteArray &data);
    void debugRequest(const QNetworkRequest &request, const QString &data);
    void debugReply(const QNetworkReply &reply, const QByteArray &data);
    void debugReplyAndReadAll(QNetworkReply *reply);

    QString debuggingString(const QNetworkRequest &request, const QByteArray &data);
    QString debuggingString(const QNetworkReply &reply, const QByteArray &data);

    QNetworkAccessManager *mNAManager;
    const QString REQUEST_TYPE;
    Settings* mSettings;
    QNetworkReply::NetworkError mNetworkError;
    bool mErrorOccurred;
    QString mErrorMessage;
    QByteArray mErrorData;
};

#endif // REQUEST_H
