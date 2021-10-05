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

#include "put.h"
#include "report.h"
#include "settings.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QBuffer>
#include <QDebug>
#include <QStringList>
#include <QUrl>

#include "logging.h"

#define PROP_INCIDENCE_URI "uri"

Put::Put(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "PUT", parent)
{
}

void Put::sendIcalData(const QString &uri, const QString &icalData,
                       const QString &eTag)
{
    FUNCTION_CALL_TRACE(lcCalDavTrace);

    if (uri.isEmpty()) {
        finishedWithInternalError("no uri provided");
        return;
    }
    if (mLocalUriList.contains(uri)) {
        finishedWithInternalError("already uploaded ical data to uri");
        return;
    }

    mLocalUriList.insert(uri);
    QByteArray data = icalData.toUtf8();
    if (data.isEmpty()) {
        finishedWithInternalError("no ical data provided or cannot convert data to UTF-8");
        return;
    }

    QNetworkRequest request;
    prepareRequest(&request, uri);
    if (eTag.isEmpty()) {
        request.setRawHeader("If-None-Match", "*");
    } else {
        request.setRawHeader("If-Match", eTag.toLatin1());
    }
    request.setHeader(QNetworkRequest::ContentLengthHeader, data.length());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/calendar; charset=utf-8");

    QNetworkReply *reply = mNAManager->put(request, data);
    reply->setProperty(PROP_INCIDENCE_URI, uri);
    debugRequest(request, data);
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Put::handleReply(QNetworkReply *reply)
{
    FUNCTION_CALL_TRACE(lcCalDavTrace);

    // If the put was denied by server (e.g. read-only calendar), the etag
    // is not updated, so NotebookSyncAgent::finalizeSendingLocalChanges()
    // will emit a rollback report for this incidence.
    const QString &uri = reply->property(PROP_INCIDENCE_URI).toString();
    if (reply->error() != QNetworkReply::ContentOperationNotPermittedError) {
        // Server may update the etag as soon as the modification is received and send back a new etag
        for (const QNetworkReply::RawHeaderPair &header : reply->rawHeaderPairs()) {
            if (header.first.toLower() == QByteArray("etag")) {
                mUpdatedETags.insert(uri, header.second);
            }
        }
    }
    mLocalUriList.remove(uri);

    finishedWithReplyResult(uri, reply);
}

QString Put::updatedETag(const QString &uri) const
{
    return mUpdatedETags.value(uri, QString());
}
