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
#include "incidencehandler.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QBuffer>
#include <QDebug>
#include <QStringList>
#include <QUrl>

#include <incidence.h>
#include <icalformat.h>

#include <LogMacros.h>

#define PROP_INCIDENCE_URI "uri"

Put::Put(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "PUT", parent)
{
}

void Put::updateEvent(const QString &uri, const QString &icalData, const QString &eTag)
{
    FUNCTION_CALL_TRACE;

    if (mLocalUriList.contains(uri)) {
        LOG_WARNING("Already uploaded modification to event at uri:" << uri);
        return;
    }

    mLocalUriList.insert(uri);
    QByteArray data = icalData.toUtf8();
    if (data.isEmpty()) {
        LOG_WARNING("Error while converting iCal Object to QByteArray");
        return;
    }

    QNetworkRequest request;
    prepareRequest(&request, uri);
    request.setRawHeader("If-Match", eTag.toLatin1());
    request.setHeader(QNetworkRequest::ContentLengthHeader, data.length());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/calendar; charset=utf-8");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData(data);
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    reply->setProperty(PROP_INCIDENCE_URI, uri);
    debugRequest(request, data);
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Put::createEvent(const QString &uri, const QString &icalData)
{
    FUNCTION_CALL_TRACE;

    if (mLocalUriList.contains(uri)) {
        LOG_WARNING("Already uploaded modification to event at uri:" << uri);
        return;
    }

    mLocalUriList.insert(uri);
    QByteArray data = icalData.toUtf8();
    if (data.isEmpty()) {
        LOG_WARNING("Error while converting iCal Object to QByteArray");
        return;
    }

    QNetworkRequest request;
    prepareRequest(&request, uri);
    request.setRawHeader("If-None-Match", "*");
    request.setHeader(QNetworkRequest::ContentLengthHeader, data.length());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/calendar; charset=utf-8");

    QBuffer *buffer = new QBuffer(this);
    buffer->setData(data);
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    reply->setProperty(PROP_INCIDENCE_URI, uri);
    LOG_DEBUG("Sent create event request:" << REQUEST_TYPE.toLatin1() << request.url().toString());
    debugRequest(request, data);
    connect(reply, SIGNAL(finished()), this, SLOT(requestFinished()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void Put::requestFinished()
{
    FUNCTION_CALL_TRACE;

    if (wasDeleted()) {
        LOG_DEBUG(command() << "request was aborted");
        return;
    }

    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        LOG_WARNING("Internal error: PUT request finished but null");
        finishedWithInternalError();
        return;
    }

    LOG_DEBUG("PUT request finished:" << reply->error());
    debugReplyAndReadAll(reply);

    // If the put was denied by server (e.g. read-only calendar), the etag
    // is not updated, so NotebookSyncAgent::finalizeSendingLocalChanges()
    // will emit a rollback report for this incidence.
    if (reply->error() != QNetworkReply::ContentOperationNotPermittedError) {
        // Server may update the etag as soon as the modification is received and send back a new etag
        Q_FOREACH (const QNetworkReply::RawHeaderPair &header, reply->rawHeaderPairs()) {
            if (header.first.toLower() == QStringLiteral("etag")) {
                mUpdatedETags.insert(reply->property(PROP_INCIDENCE_URI).toString(), header.second);
            }
        }
    }

    finishedWithReplyResult(reply->error());
    reply->deleteLater();
}

const QHash<QString,QString>& Put::updatedETags() const
{
    return mUpdatedETags;
}
