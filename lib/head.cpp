/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2025 Damien Caliste <dcaliste@free.fr>
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

#include "head_p.h"

#include <QNetworkAccessManager>

#define PROP_URI "uri"

Head::Head(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "HEAD", parent)
{
}

void Head::getServiceUrl(const QString &service)
{
    mServiceUrls.remove(service);
    sendRequest(QString::fromLatin1("/.well-known/%1").arg(service), ServiceUrl);
}

void Head::sendRequest(const QString &remotePath, HeadRequestType type)
{
    QNetworkRequest request;
    prepareRequest(&request, remotePath);
    request.setRawHeader("Prefer", "return-minimal");
    QNetworkReply *reply = mNAManager->head(request);
    reply->setProperty(PROP_URI, remotePath);
    mType = type;
    debugRequest(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, &Head::requestFinished);
    connect(reply, &QNetworkReply::sslErrors, this, &Head::slotSslErrors);
}

void Head::handleReply(QNetworkReply *reply)
{
    const QString &uri = reply->property(PROP_URI).toString();
    if (reply->error() != QNetworkReply::NoError) {
        finishedWithReplyResult(uri, reply);
        return;
    }
    
    const QByteArray data = reply->readAll();
    debugReply(*reply, data);

    if (mType == ServiceUrl) {
        // By RFC 6764, it must be a redirection
        QUrl location = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (location.isValid()) {
            mServiceUrls.insert(uri.mid(uri.lastIndexOf("/") + 1), location);
        } else {
            finishedWithError(uri, QString("No redirection available for .well-known"), data);
        }
    }
    
    finishedWithSuccess(uri);
}

QUrl Head::serviceUrl(const QString &service) const
{
    return mServiceUrls.value(service);
}
