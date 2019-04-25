/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2019 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Damien Caliste <dcaliste@free.fr>
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

#include "propfind.h"
#include "settings.h"

#include <QNetworkAccessManager>
#include <QBuffer>
#include <QXmlStreamReader>

#include <LogMacros.h>

static bool readResourceType(QXmlStreamReader *reader, bool *isCalendar)
{
    for (; !reader->atEnd(); reader->readNext()) {
        if (reader->name() == "calendar") {
            *isCalendar = true;
        }
        if (reader->name() == "resourcetype" && reader->isEndElement()) {
            return true;
        }
    }
    return false;
}

static bool readProp(QXmlStreamReader *reader, bool *isCalendar, QString *label, QString *color)
{
    QString displayName;
    QString displayColor;
    *isCalendar = false;
    for (; !reader->atEnd(); reader->readNext()) {
        if (reader->name() == "displayname" && reader->isStartElement()) {
            displayName = reader->readElementText();
        }
        if (reader->name() == "calendar-color" && reader->isStartElement()) {
            displayColor = reader->readElementText();
            if (displayColor.startsWith("#") && displayColor.length() == 9) {
                displayColor = displayColor.left(7);
            }
        }
        if (reader->name() == "resourcetype") {
            if (!readResourceType(reader, isCalendar)) {
                return false;
            }
        }
        if (reader->name() == "prop" && reader->isEndElement()) {
            if (*isCalendar) {
                *label = displayName.isEmpty() ? QStringLiteral("Calendar") : displayName;
                *color = displayColor;
            }
            return true;
        }
    }
    return false;
}

static bool readPropStat(QXmlStreamReader *reader, bool *isCalendar, QString *label, QString *color)
{
    for (; !reader->atEnd(); reader->readNext()) {
        if (reader->name() == "prop") {
            if (!readProp(reader, isCalendar, label, color)) {
                return false;
            }
        }
        if (reader->name() == "propstat" && reader->isEndElement()) {
            return true;
        }
    }
    return false;
}

static bool readResponse(QXmlStreamReader *reader, QList<Settings::CalendarInfo> *calendars)
{
    bool isCalendar = false;
    QString href;
    QString label;
    QString color;
    for (; !reader->atEnd(); reader->readNext()) {
        if (reader->name() == "href" && reader->isStartElement()) {
            href = reader->readElementText();
        }
        if (reader->name() == "propstat") {
            if (!readPropStat(reader, &isCalendar, &label, &color)) {
                return false;
            }
        }
        if (reader->name() == "response" && reader->isEndElement()) {
            if (!isCalendar) {
                return true;
            }
            if (href.isEmpty()) {
                return false;
            }
            calendars->append(Settings::CalendarInfo{href, label, color});
            return true;
        }
    }
    return false;
}

PropFind::PropFind(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "PROPFIND", parent)
{
    FUNCTION_CALL_TRACE;
}

void PropFind::listCalendars(const QString &calendarsPath)
{
    QByteArray requestData("<d:propfind xmlns:d=\"DAV:\" xmlns:a=\"http://apple.com/ns/ical/\">"   \
                           " <d:prop>"                       \
                           "  <d:resourcetype />"            \
                           "  <d:current-user-principal />"  \
                           "  <d:displayname />"             \
                           "  <a:calendar-color />"         \
                           " </d:prop>"                      \
                           "</d:propfind>");
    mCalendars.clear();
    sendRequest(calendarsPath, requestData);
}

void PropFind::sendRequest(const QString &remotePath, const QByteArray &requestData)
{
    FUNCTION_CALL_TRACE;

    QNetworkRequest request;
    prepareRequest(&request, remotePath);
    request.setRawHeader("Depth", "1");
    request.setRawHeader("Prefer", "return-minimal");
    request.setHeader(QNetworkRequest::ContentLengthHeader, requestData.length());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");
    QBuffer *buffer = new QBuffer(this);
    buffer->setData(requestData);
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    debugRequest(request, buffer->buffer());
    connect(reply, SIGNAL(finished()), this, SLOT(processResponse()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void PropFind::processResponse()
{
    FUNCTION_CALL_TRACE;

    LOG_DEBUG("Process PROPFIND response.");

    if (wasDeleted()) {
        LOG_DEBUG("PROPFIND request was aborted");
        return;
    }

    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        finishedWithInternalError();
        return;
    }
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        finishedWithReplyResult(reply->error());
        return;
    }
    QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCode.isValid()) {
        int status = statusCode.toInt();
        if (status > 299) {
            finishedWithError(Buteo::SyncResults::INTERNAL_ERROR,
                              QString("Got error status response for PROPFIND: %1").arg(status));
            return;
        }
    }

    QByteArray data = reply->readAll();
    debugReply(*reply, data);

    if (data.isNull() || data.isEmpty()) {
        finishedWithError(Buteo::SyncResults::INTERNAL_ERROR,
                          QString("Empty response body for PROPFIND"));
        return;
    }

    QXmlStreamReader reader(data);
    reader.setNamespaceProcessing(true);
    for (; !reader.atEnd(); reader.readNext()) {
        if (reader.name() == "response") {
            if (!readResponse(&reader, &mCalendars)) {
                finishedWithError(Buteo::SyncResults::INTERNAL_ERROR,
                                  QString("Cannot parse response body for PROPFIND"));
                return;
            }
        }
    }

    finishedWithSuccess();
}

const QList<Settings::CalendarInfo>& PropFind::calendars() const
{
    return mCalendars;
}
