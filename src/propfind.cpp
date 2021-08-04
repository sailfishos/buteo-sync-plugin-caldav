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

#include "logging.h"

#define PROP_URI "uri"

static bool readResourceType(QXmlStreamReader *reader, bool *isCalendar)
{
    /* e.g.:
        <D:resourcetype><C:calendar xmlns:C=\"urn:ietf:params:xml:ns:caldav\"/><D:collection/></D:resourcetype>
    */
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

static bool readPrivilegeSet(QXmlStreamReader *reader, bool *readOnly)
{
    /* e.g.:
                    <D:current-user-privilege-set>
                        <D:privilege><D:read /></D:privilege>
                        <D:privilege><D:write /></D:privilege>
                        <D:privilege><D:write-content /></D:privilege>
                        <D:privilege><D:bind /></D:privilege>
                        <D:privilege><D:unbind /></D:privilege>
                    </D:current-user-privilege-set>
    */
    bool readPriv = false;
    bool writePriv = false;
    for (; !reader->atEnd(); reader->readNext()) {
        if (reader->name() == "read") {
            readPriv = true;
        } else if (reader->name() == "write") {
            writePriv = true;
        } else if (reader->name() == "current-user-privilege-set"
                   && reader->isEndElement()) {
            *readOnly = readPriv && !writePriv;
            return true;
        }
    }
    return false;
}

static bool readCalendarProp(QXmlStreamReader *reader, bool *isCalendar, QString *label, QString *color, QString *userPrincipal, bool *readOnly)
{
    /* e.g.:
        <D:prop>
            <D:displayname>My events</D:displayname>
            <calendar-color xmlns=\"http://apple.com/ns/ical/\">#4887e1ff</calendar-color>
            <D:resourcetype><C:calendar xmlns:C=\"urn:ietf:params:xml:ns:caldav\"/><D:collection/></D:resourcetype>
        </D:prop>
    */
    QString displayName;
    QString displayColor;
    QString currentUserPrincipal;
    bool readOnlyStatus = false;
    *isCalendar = false;
    for (; !reader->atEnd(); reader->readNext()) {
        if (reader->name() == "displayname" && reader->isStartElement()) {
            displayName = reader->readElementText();
        } else if (reader->name() == "calendar-color" && reader->isStartElement()) {
            displayColor = reader->readElementText();
            if (displayColor.startsWith("#") && displayColor.length() == 9) {
                displayColor = displayColor.left(7);
            }
        } else if (reader->name() == "current-user-principal" && reader->isStartElement()) {
            for (;!reader->atEnd(); reader->readNext()) {
                if (reader->name() == "href" && reader->isStartElement()) {
                    currentUserPrincipal = reader->readElementText();
                    break;
                } else if (reader->name() == "current-user-principal" && reader->isEndElement()) {
                    break;
                }
            }
        } else if (reader->name() == "resourcetype" && reader->isStartElement()) {
            if (!readResourceType(reader, isCalendar)) {
                return false;
            }
        } else if (reader->name() == "current-user-privilege-set" && reader->isStartElement()) {
            if (!readPrivilegeSet(reader, &readOnlyStatus)) {
                return false;
            }
        } else if (reader->name() == "prop" && reader->isEndElement()) {
            if (*isCalendar) {
                *label = displayName.isEmpty() ? QStringLiteral("Calendar") : displayName;
                *color = displayColor;
                *userPrincipal = currentUserPrincipal;
                *readOnly = readOnlyStatus;
            }
            return true;
        }
    }
    return false;
}

static bool readCalendarPropStat(QXmlStreamReader *reader, bool *isCalendar, QString *label, QString *color, QString *userPrincipal, bool *readOnly)
{
    /* e.g.:
        <D:propstat>
            <D:prop>
                <D:displayname>My events</D:displayname>
                <calendar-color xmlns=\"http://apple.com/ns/ical/\">#4887e1ff</calendar-color>
                <D:resourcetype><C:calendar xmlns:C=\"urn:ietf:params:xml:ns:caldav\"/><D:collection/></D:resourcetype>
            </D:prop>
            <status xmlns=\"DAV:\">HTTP/1.1 200 OK</status>
        </D:propstat>
    */
    for (; !reader->atEnd(); reader->readNext()) {
        if (reader->name() == "prop" && reader->isStartElement()) {
            if (!readCalendarProp(reader, isCalendar, label, color, userPrincipal, readOnly)) {
                return false;
            }
        } else if (reader->name() == "propstat" && reader->isEndElement()) {
            return true;
        }
    }
    return false;
}

static bool readCalendarsResponse(QXmlStreamReader *reader, QList<PropFind::CalendarInfo> *calendars)
{
    /* e.g.:
        <D:response>
            <href xmlns=\"DAV:\">/calendars/username%40server.tld/events-calendar/</href>
            <D:propstat>
                <D:prop>
                    <D:displayname>My events</D:displayname>
                    <calendar-color xmlns=\"http://apple.com/ns/ical/\">#4887e1ff</calendar-color>
                    <D:resourcetype><C:calendar xmlns:C=\"urn:ietf:params:xml:ns:caldav\"/><D:collection/></D:resourcetype>
                    <D:current-user-privilege-set>
                        <D:privilege><D:read /></D:privilege>
                        <D:privilege><D:write /></D:privilege>
                        <D:privilege><D:write-content /></D:privilege>
                        <D:privilege><D:bind /></D:privilege>
                        <D:privilege><D:unbind /></D:privilege>
                    </D:current-user-privilege-set>
                </D:prop>
                <status xmlns=\"DAV:\">HTTP/1.1 200 OK</status>
            </D:propstat>
            <D:propstat>
                <D:prop>
                    <D:current-user-principal/>
                </D:prop>
                <status xmlns=\"DAV:\">HTTP/1.1 404 Not Found</status>
            </D:propstat>
        </D:response>
    */

    bool responseIsCalendar = false;
    bool hasPropStat = false;
    PropFind::CalendarInfo calendarInfo;
    for (; !reader->atEnd(); reader->readNext()) {
        if (reader->name() == "href" && reader->isStartElement() && calendarInfo.remotePath.isEmpty()) {
            // The account stores this with the encoding, so we're converting from
            // percent encoding later.
            calendarInfo.remotePath = reader->readElementText();
        }

        if (reader->name() == "propstat" && reader->isStartElement()) {
            bool propStatIsCalendar = false;
            PropFind::CalendarInfo tempCalendarInfo;
            if (!readCalendarPropStat(reader, &propStatIsCalendar,
                                      &tempCalendarInfo.displayName,
                                      &tempCalendarInfo.color,
                                      &tempCalendarInfo.userPrincipal,
                                      &tempCalendarInfo.readOnly)) {
                return false;
            } else if (propStatIsCalendar) {
                responseIsCalendar = true;
                calendarInfo.displayName = tempCalendarInfo.displayName;
                calendarInfo.color = tempCalendarInfo.color;
                calendarInfo.userPrincipal = tempCalendarInfo.userPrincipal.trimmed();
                calendarInfo.readOnly = tempCalendarInfo.readOnly;
            }
            hasPropStat = true;
        }

        if (reader->name() == "response" && reader->isEndElement()) {
            if (!responseIsCalendar) {
                return hasPropStat;
            }

            if (calendarInfo.remotePath.isEmpty()) {
                return false;
            }

            calendars->append(calendarInfo);
            return true;
        }
    }

    return false;
}

static bool readUserAddressSetResponse(QXmlStreamReader *reader, QString *mailtoHref, QString *homeHref)
{
    /* expect a response like:
        <?xml version='1.0' encoding='utf-8'?>
        <D:multistatus xmlns:D="DAV:">
            <D:response>
                <href xmlns="DAV:">/principals/users/username%40server.tld/</href>
                <D:propstat>
                    <D:prop>
                        <C:calendar-home-set xmlns:C="urn:ietf:params:xml:ns:caldav">
                            <D:href>/caldav/</D:href>
                        </C:calendar-home-set>
                        <C:calendar-user-address-set xmlns:C="urn:ietf:params:xml:ns:caldav">
                            <D:href>mailto:username@server.tld</D:href>
                            <D:href>/principals/users/username%40server.tld/</D:href>
                        </C:calendar-user-address-set>
                    </D:prop>
                    <status xmlns="DAV:">HTTP/1.1 200 OK</status>
                </D:propstat>
            </D:response>
        </D:multistatus>
    */

    bool canReadMailtoHref = false;
    bool canReadHomeHref = false;
    bool valid = false;
    for (; !reader->atEnd(); reader->readNext()) {
        if (reader->name() == "calendar-user-address-set") {
            canReadMailtoHref = reader->isStartElement();
        } else if (reader->name() == "calendar-home-set") {
            canReadHomeHref = reader->isStartElement();
        } else if (canReadMailtoHref
                   && reader->name() == "href" && reader->isStartElement()) {
            valid = true;
            QString href = reader->readElementText();
            if (href.startsWith(QStringLiteral("mailto:"), Qt::CaseInsensitive)) {
                *mailtoHref = href.mid(7); // chop off "mailto:"
            }
        } else if (canReadHomeHref
                   && reader->name() == "href" && reader->isStartElement()) {
            valid = true;
            *homeHref = reader->readElementText();
        } else if (reader->name() == "propstat" && reader->isEndElement()) {
            return valid;
        }
    }

    return false;
}

static bool readUserPrincipalResponse(QXmlStreamReader *reader, QString *userPrincipal)
{
    /* expect a response like:
        <?xml version='1.0' encoding='utf-8'?>
        <D:multistatus xmlns:D="DAV:">
            <D:response>
                <href xmlns="DAV:">/</href>
                <D:propstat>
                    <D:prop>
                        <D:current-user-principal>
                            <D:href>/principals/users/username%40server.tld/</D:href>
                        </D:current-user-principal>
                    </D:prop>
                    <status xmlns="DAV:">HTTP/1.1 200 OK</status>
                </D:propstat>
            </D:response>
        </D:multistatus>
    */

    QString href;
    bool canReadUserPrincipalHref = false;
    for (; !reader->atEnd(); reader->readNext()) {
        if (reader->name() == "current-user-principal") {
            if (reader->isStartElement()) {
                canReadUserPrincipalHref = true;
            } else if (reader->isEndElement()) {
                canReadUserPrincipalHref = false;
                if (href.isEmpty()) {
                    return false;
                }
                *userPrincipal = href;
                return true;
            }
        } else if (reader->name() == "href"
                && reader->isStartElement()
                && canReadUserPrincipalHref) {
            href = reader->readElementText();
        }
    }

    return false;
}

bool PropFind::parseCalendarResponse(const QByteArray &data)
{
    if (data.isNull() || data.isEmpty()) {
        return false;
    }
    QXmlStreamReader reader(data);
    reader.setNamespaceProcessing(true);
    for (; !reader.atEnd(); reader.readNext()) {
        if (reader.name() == "response" && reader.isStartElement()
                && !readCalendarsResponse(&reader, &mCalendars)) {
            return false;
        }
    }
    return true;
}

bool PropFind::parseUserPrincipalResponse(const QByteArray &data)
{
    if (data.isNull() || data.isEmpty()) {
        return false;
    }
    QXmlStreamReader reader(data);
    reader.setNamespaceProcessing(true);
    for (; !reader.atEnd(); reader.readNext()) {
        if (reader.name() == "response" && reader.isStartElement()
                && !readUserPrincipalResponse(&reader, &mUserPrincipal)) {
            return false;
        }
    }
    return true;
}

bool PropFind::parseUserAddressSetResponse(const QByteArray &data)
{
    if (data.isNull() || data.isEmpty()) {
        return false;
    }
    QXmlStreamReader reader(data);
    reader.setNamespaceProcessing(true);
    for (; !reader.atEnd(); reader.readNext()) {
        if (reader.name() == "response" && reader.isStartElement()
                && !readUserAddressSetResponse(&reader, &mUserMailtoHref, &mUserHomeHref)) {
            return false;
        }
    }
    return true;
}

PropFind::PropFind(QNetworkAccessManager *manager, Settings *settings, QObject *parent)
    : Request(manager, settings, "PROPFIND", parent)
{
    FUNCTION_CALL_TRACE(lcCalDavTrace);
}

void PropFind::listCalendars(const QString &calendarsPath)
{
    QByteArray requestData("<d:propfind xmlns:d=\"DAV:\" xmlns:a=\"http://apple.com/ns/ical/\">"   \
                           " <d:prop>"                       \
                           "  <d:resourcetype />"            \
                           "  <d:current-user-principal />"  \
                           "  <d:current-user-privilege-set />"  \
                           "  <d:displayname />"             \
                           "  <a:calendar-color />"         \
                           " </d:prop>"                      \
                           "</d:propfind>");
    mCalendars.clear();
    sendRequest(calendarsPath, requestData, ListCalendars);
}

void PropFind::listUserAddressSet(const QString &userPrincipal)
{
    const QByteArray requestData(QByteArrayLiteral(
            "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
            "  <d:prop>"
            "    <c:calendar-user-address-set />"
            "    <c:calendar-home-set />"
            "  </d:prop>"
            "</d:propfind>"
    ));
    mUserMailtoHref.clear();
    mUserHomeHref.clear();
    sendRequest(userPrincipal, requestData, UserAddressSet);
}

void PropFind::listCurrentUserPrincipal()
{
    const QByteArray requestData(QByteArrayLiteral(
            "<d:propfind xmlns:d=\"DAV:\">"
            "  <d:prop>"
            "    <d:current-user-principal />"
            "  </d:prop>"
            "</d:propfind>"
    ));
    mUserPrincipal.clear();
    const QString &rootPath = mSettings->davRootPath();
    sendRequest(rootPath.isEmpty() ? QStringLiteral("/") : rootPath,
                requestData, UserPrincipal);
}

void PropFind::sendRequest(const QString &remotePath, const QByteArray &requestData, PropFindRequestType reqType)
{
    FUNCTION_CALL_TRACE(lcCalDavTrace);

    mPropFindRequestType = reqType;

    QNetworkRequest request;
    prepareRequest(&request, remotePath);
    if (reqType == ListCalendars)
        request.setRawHeader("Depth", "1");
    else
        request.setRawHeader("Depth", "0");
    request.setRawHeader("Prefer", "return-minimal");
    request.setHeader(QNetworkRequest::ContentLengthHeader, requestData.length());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");
    QBuffer *buffer = new QBuffer(this);
    buffer->setData(requestData);
    // TODO: when Qt5.8 is available, remove the use of buffer, and pass requestData directly.
    QNetworkReply *reply = mNAManager->sendCustomRequest(request, REQUEST_TYPE.toLatin1(), buffer);
    reply->setProperty(PROP_URI, remotePath);
    debugRequest(request, buffer->buffer());
    connect(reply, SIGNAL(finished()), this, SLOT(processResponse()));
    connect(reply, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(slotSslErrors(QList<QSslError>)));
}

void PropFind::processResponse()
{
    FUNCTION_CALL_TRACE(lcCalDavTrace);

    qCDebug(lcCalDav) << "Process PROPFIND response.";

    if (wasDeleted()) {
        qCDebug(lcCalDav) << "PROPFIND request was aborted";
        return;
    }

    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        finishedWithInternalError(QString());
        return;
    }
    reply->deleteLater();
    const QString &uri = reply->property(PROP_URI).toString();
    if (reply->error() != QNetworkReply::NoError) {
        debugReplyAndReadAll(reply);
        finishedWithReplyResult(uri, reply->error());
        return;
    }
    QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusCode.isValid()) {
        int status = statusCode.toInt();
        if (status > 299) {
            finishedWithError(uri, Buteo::SyncResults::INTERNAL_ERROR,
                              QString("Got error status response for PROPFIND: %1").arg(status));
            return;
        }
    }

    QByteArray data = reply->readAll();
    debugReply(*reply, data);
    bool success = false;
    switch (mPropFindRequestType) {
    case (UserPrincipal):
        success = parseUserPrincipalResponse(data);
        break;
    case (UserAddressSet):
        success = parseUserAddressSetResponse(data);
        break;
    case (ListCalendars):
        success = parseCalendarResponse(data);
        break;
    }
    if (success) {
        finishedWithSuccess(uri);
    } else {
        finishedWithError(uri, Buteo::SyncResults::INTERNAL_ERROR,
                          QString("Cannot parse response body for PROPFIND"));
    }
}

const QList<PropFind::CalendarInfo>& PropFind::calendars() const
{
    return mCalendars;
}

QString PropFind::userPrincipal() const
{
    return mUserPrincipal;
}

QString PropFind::userMailtoHref() const
{
    return mUserMailtoHref;
}

QString PropFind::userHomeHref() const
{
    return mUserHomeHref;
}
