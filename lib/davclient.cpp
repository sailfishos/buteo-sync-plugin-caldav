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
 */

#include "davclient.h"

#include <QNetworkAccessManager>

#include "settings_p.h"
#include "request_p.h"
#include "propfind_p.h"
#include "report_p.h"
#include "head_p.h"
#include "put_p.h"
#include "delete_p.h"
#include "logging_p.h"

namespace {
    Buteo::Dav::Client::Reply reply(const Request &request, const QString &path)
    {
        return Buteo::Dav::Client::Reply(path, request.networkError(),
                                         request.errorMessage(),
                                         request.errorData());
    }

    QString ensureRoot(const QString &path)
    {
        if (path.startsWith(QChar('/')))
            return path;
        else
            return QString::fromLatin1("/%1").arg(path);
    }
}

class Buteo::Dav::ClientPrivate
{
public:
    ClientPrivate(const QString &serverAddress)
    {
        // Todo: use .chopped(1) here from Qt5.10.
        m_settings.setServerAddress(serverAddress.endsWith(QChar('/'))
                                    ? serverAddress.left(serverAddress.length() - 1)
                                    : serverAddress);
    }

    ~ClientPrivate()
    {
    }

    void setCalendarList(const QList<Buteo::Dav::CalendarInfo> &serverList)
    {
        if (m_settings.serverAddress().endsWith(QStringLiteral(".memotoo.com"))) {
            // Memotoo has a single calendar, and is using categories
            // to sort events. It's possible to list events on categories
            // using a specific path for the request. But it's not possible
            // to send events in a specific category without mentioning
            // this category in the ICS data. While the server is exposing the
            // categories as a list of calendars in a PROPFIND request, we
            // should report a single calendar, valid for all categories,
            // so the user can view all the events.
            m_calendars.clear();
            for (const Buteo::Dav::CalendarInfo &info : serverList) {
                if (info.remotePath.endsWith(QStringLiteral("/category0/"))) {
                    m_calendars << info;
                    m_calendars[0].remotePath.chop(10);
                    break;
                }
            }
        } else {
            m_calendars = serverList;
        }
    }

    Settings m_settings;
    QNetworkAccessManager *m_networkManager;
    bool m_wellKnowRetryInProgress = false;
    QString m_userPrincipal;
    QMap<QString, PropFind::UserAddressSet> m_serviceData;
    QList<Buteo::Dav::CalendarInfo> m_calendars;
};

/*!
  \class Client

  \preliminary
  \brief A client implementation for DAV operations.

  Instances of this class can be used to perform DAV operations.
*/

/*!
  Create a new client to perform DAV operations on \param serverAddress.
  The server address should have the form as http[s]://dav.example.org.
*/
Buteo::Dav::Client::Client(const QString &serverAddress, QObject *parent)
    : QObject(parent), d(new ClientPrivate(serverAddress))
{
    d->m_networkManager = new QNetworkAccessManager(this);
}

Buteo::Dav::Client::~Client()
{
}

/*!
  Returns the server address as defined on construction.
*/
QString Buteo::Dav::Client::serverAddress() const
{
    return d->m_settings.serverAddress();
}

/*!
  Returns true if the client should ignore SSL errors, like self-signed certificates.
*/
bool Buteo::Dav::Client::ignoreSSLErrors() const
{
    return d->m_settings.ignoreSSLErrors();
}

/*!
  Set if the client should ignore SSL errors.
*/
void Buteo::Dav::Client::setIgnoreSSLErrors(bool ignore)
{
    d->m_settings.setIgnoreSSLErrors(ignore);
}

/*!
  Provide the login:password couple for a basic authentication on the server.
*/
void Buteo::Dav::Client::setAuthLogin(const QString &username, const QString &password)
{
    d->m_settings.setUsername(username);
    d->m_settings.setPassword(password);
}

/*!
  Provide the token for a bearer authentication on the server.
*/
void Buteo::Dav::Client::setAuthToken(const QString &token)
{
    d->m_settings.setAuthToken(token);
}

/*!
  Inquire the server about the logged-in user and the main information
  about the various DAV services the server provide. When \param service
  is not empty, it restrict the inquiry to the specified service.

  The list of inquired services can be obtained with services().

  In case the information from the logged-in user is not available,
  a fallback strategy is to only get \param service path, using the
  .well-known/service mechanism. This is only possible when \param
  service is not empty.

  If the DAV services are not available on the root '/' of the server,
  it is possible to give the \param davPath where the services are
  available at. If the DAV path is not known, it can be guessed using
  the .well-known/service mechanism, but only if \param service is
  provided.

  \sa userPrincipal(), services(), servicePath() and serviceMailto().
*/
void Buteo::Dav::Client::requestUserPrincipalAndServiceData(const QString &service,
                                                            const QString &davPath)
{
    d->m_userPrincipal.clear();
    d->m_serviceData.clear();
    PropFind *userRequest = new PropFind(d->m_networkManager, &d->m_settings, this);
    connect(userRequest, &Request::finished, this,
            [this, userRequest, service] (const QString &uri) {
        userRequest->deleteLater();

        const QString userPrincipal = userRequest->userPrincipal();
        if (!userRequest->hasError() && !userPrincipal.isEmpty()) {
            d->m_userPrincipal = userPrincipal;
            // determine the mailto href for this user.
            PropFind *hrefsRequest = new PropFind(d->m_networkManager, &d->m_settings, this);
            connect(hrefsRequest, &Request::finished, this,
                    [this, hrefsRequest] (const QString &uri) {
                hrefsRequest->deleteLater();

                if (!hrefsRequest->hasError()) {
                    d->m_serviceData = hrefsRequest->userAddressSets();
                }
                d->m_wellKnowRetryInProgress = false;
                emit userPrincipalDataFinished(reply(*hrefsRequest, uri));
            });
            hrefsRequest->listUserAddressSet(userPrincipal, service);
        } else if (!service.isEmpty() && !d->m_wellKnowRetryInProgress) {
            // Can't find a user principal, try with a .well-known redirection.
            Head *serviceRequest = new Head(d->m_networkManager, &d->m_settings, this);
            connect(serviceRequest, &Request::finished, this,
                    [this, serviceRequest, service] (const QString &uri) {
                serviceRequest->deleteLater();

                if (!serviceRequest->hasError()) {
                    const QUrl url = serviceRequest->serviceUrl(service);
                    // Redirection may point to a different [sub]domain.
                    d->m_settings.setServerAddress(QString::fromLatin1("%1://%2").arg(url.scheme()).arg(url.host()));
                    // Retry to get a user principal using the provided redirect.
                    d->m_wellKnowRetryInProgress = true;
                    requestUserPrincipalAndServiceData(service, url.path());
                } else {
                    emit userPrincipalDataFinished(reply(*serviceRequest, uri));
                }
            });
            serviceRequest->getServiceUrl(service);
        } else {
            d->m_wellKnowRetryInProgress = false;
            emit userPrincipalDataFinished(reply(*userRequest, uri));
        }
    });
    userRequest->listCurrentUserPrincipal(ensureRoot(davPath));
}

/*!
  Returns the path used to identify the logged-in user. It is available
  after userPrincipalDataFinished() signal has been triggered.

  \sa requestUserPrincipalAndServiceData()
*/
QString Buteo::Dav::Client::userPrincipal() const
{
    return d->m_userPrincipal;
}

/*!
  Returns the list of discovered (and supported) services on the DAV server.
  It is available after userPrincipalDataFinished() signal has been triggered
  and only if the logged-in user has been identified.

  \sa requestUserPrincipalAndServiceData()
*/
QStringList Buteo::Dav::Client::services() const
{
    return d->m_serviceData.keys();
}

/*!
  Returns the email address declared by the user for \param service.
  It is available after userPrincipalDataFinished() signal has been triggered
  and only if the logged-in user has been identified.

  \sa requestUserPrincipalAndServiceData()
*/
QString Buteo::Dav::Client::serviceMailto(const QString &service) const
{
    return d->m_serviceData[service].mailto;
}

/*!
  Returns the path at which \param service is running on the server.

  \sa requestUserPrincipalAndServiceData()
*/
QString Buteo::Dav::Client::servicePath(const QString &service) const
{
    return d->m_serviceData[service].path;
}

/*!
  Request the list of VCALENDAR available at \param path on the server.
  If \param path is empty, the path registered for "caldav" service is
  used (when properly discovered).

  \sa services() and calendars()
*/
void Buteo::Dav::Client::requestCalendarList(const QString &path)
{
    d->m_calendars.clear();
    PropFind *calendarRequest = new PropFind(d->m_networkManager, &d->m_settings, this);
    connect(calendarRequest, &Request::finished, this,
            [this, calendarRequest] (const QString &uri) {
        calendarRequest->deleteLater();

        if (!calendarRequest->hasError()
            // Request silently ignores this QNetworkReply::NetworkError
            && calendarRequest->networkError() != QNetworkReply::ContentOperationNotPermittedError) {
            d->setCalendarList(calendarRequest->calendars());
        }
        emit calendarListFinished(reply(*calendarRequest, uri));
    });
    calendarRequest->listCalendars(path.isEmpty() ? servicePath(QStringLiteral("caldav")) : path);
}

/*!
  Returns the list of available on the server calendars. This list is available
  only after calendarListFinished() signal has been triggered.

  \sa requestCalendarList()
*/
QList<Buteo::Dav::CalendarInfo> Buteo::Dav::Client::calendars() const
{
    return d->m_calendars;
}

/*!
  Request the list of etags for any calendar resources available at \param path
  which occur within \param from and \param to.

  The list of etags for every found resource will be exposed in the
  calendarEtagsFinished() signal, as a map between resource path and etag.
*/
void Buteo::Dav::Client::getCalendarEtags(const QString &path,
                                   const QDateTime &from, const QDateTime &to)
{
    Report *report = new Report(d->m_networkManager, &d->m_settings);
    connect(report, &Report::finished, this,
            [this, report] (const QString &uri) {
                report->deleteLater();

                QHash<QString, QString> etags;
                for (const Buteo::Dav::Resource &resource : report->response()) {
                    if (!resource.href.contains(uri)) {
                        qCWarning(lcDav) << "href does not contain server path:" << resource.href << ":" << uri;
                    } else {
                        etags.insert(resource.href, resource.etag);
                    }
                }
                emit calendarEtagsFinished(reply(*report, uri), etags);
            });
    report->getAllETags(path, from, to);
}

/*!
  Request the list of any calendar resources available at \param path
  which occur within \param from and \param to.

  The list of found resources will be exposed in the calendarResourcesFinished()
  signal.
*/
void Buteo::Dav::Client::getCalendarResources(const QString &path,
                                       const QDateTime &from, const QDateTime &to)
{
    Report *report = new Report(d->m_networkManager, &d->m_settings);
    connect(report, &Report::finished, this,
            [this, report] (const QString &uri) {
                report->deleteLater();

                emit calendarResourcesFinished(reply(*report, uri), report->response());
            });
    report->getAllEvents(path, from, to);
}

/*!
  Request the list of any calendar resources available at \param path
  matching the provided \param uids.

  The list of found resources will be exposed in the calendarResourcesFinished()
  signal.
*/
void Buteo::Dav::Client::getCalendarResources(const QString &path, const QStringList &uids)
{
    Report *report = new Report(d->m_networkManager, &d->m_settings);
    connect(report, &Report::finished, this,
            [this, report] (const QString &uri) {
                report->deleteLater();

                emit calendarResourcesFinished(reply(*report, uri), report->response());
            });
    report->multiGetEvents(path, uids);
}

/*!
  Send the given calendar \param data to the server at \param path location.
  When \param etag is empty, the resource must not already exist on the server.
  When \param etag is not empty, the resource on the server must match the
  provided \param etag.

  When the operation is complete, the sendCalendarFinished() signal will be
  emitted. The \param etag of this signal is the new etag of the resource as
  saved on the server. It may be empty is the server configuration don't reply
  with the new etag.
*/
void Buteo::Dav::Client::sendCalendarResource(const QString &path, const QString &data, const QString &etag)
{
    Put *put = new Put(d->m_networkManager, &d->m_settings);
    connect(put, &Put::finished, this,
            [this, put] (const QString &uri) {
                put->deleteLater();

                emit sendCalendarFinished(reply(*put, uri), put->updatedETag(uri));
            });
    put->sendIcalData(path, data, etag);
}

/*!
  Delete the resource from the server at \param path.
*/
void Buteo::Dav::Client::deleteResource(const QString &path)
{
    Delete *del = new Delete(d->m_networkManager, &d->m_settings, this);
    connect(del, &Delete::finished, this,
            [this, del] (const QString &uri) {
                del->deleteLater();

                emit deleteFinished(reply(*del, uri));
            });
    del->deleteEvent(path);
}
