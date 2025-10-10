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

#include "delete_p.h"
#include "settings_p.h"

#include <QNetworkAccessManager>
#include <QDebug>

#define PROP_INCIDENCE_URI "uri"

static const QString VCalExtension = QStringLiteral(".ics");

Delete::Delete(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "DELETE", parent)
{
}

void Delete::deleteEvent(const QString &href)
{
    QNetworkRequest request;
    prepareRequest(&request, href);
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1());
    reply->setProperty(PROP_INCIDENCE_URI, href);
    debugRequest(request, QStringLiteral(""));
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Delete::handleReply(QNetworkReply *reply)
{
    const QString &uri = reply->property(PROP_INCIDENCE_URI).toString();
    if (reply->error() == QNetworkReply::ContentNotFoundError) {
        // Consider a success if the content does not exist on server.
        finishedWithSuccess(uri);
    } else {
        finishedWithReplyResult(uri, reply);
    }
}
