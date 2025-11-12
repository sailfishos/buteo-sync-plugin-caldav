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

#ifndef DAVCLIENT_H
#define DAVCLIENT_H

#include <QObject>
#include <QScopedPointer>
#include <QDateTime>
#include <QHash>
#include <QNetworkReply>

#include "davexport.h"
#include "davtypes.h"

namespace Buteo {
namespace Dav {
class ClientPrivate;

class DAV_EXPORT Client : public QObject
{
    Q_OBJECT

public:
    struct Reply
    {
        QString uri;
        QNetworkReply::NetworkError networkError;
        QString errorMessage;
        QByteArray errorData;

        Reply(const QString &path, QNetworkReply::NetworkError error,
              const QString &message, const QByteArray &data)
            : uri(path), networkError(error), errorMessage(message), errorData(data) {}
        bool hasError() const
        {
            return networkError != QNetworkReply::NoError
                || !errorMessage.isEmpty();
        }
    };
    
    Client(const QString &serverAddress, QObject *parent = nullptr);
    Client(const QString &domain, const QString &service, QObject *parent = nullptr);
    ~Client();

    QString serverAddress() const;

    bool ignoreSSLErrors() const;
    void setIgnoreSSLErrors(bool ignore);
    
    void setAuthLogin(const QString &username, const QString &password);
    void setAuthToken(const QString &token);

    void requestUserPrincipalAndServiceData(const QString &service = QString(),
                                            const QString &davPath = QString());
    QString userPrincipal() const;
    QStringList services() const;
    QString serviceMailto(const QString &service) const;
    QString servicePath(const QString &service) const;

    // Calendar specific API (CalDAV protocol).
    void requestCalendarList(const QString &path = QString());
    QList<CalendarInfo> calendars() const;

    void getCalendarEtags(const QString &path,
                          const QDateTime &from, const QDateTime &to);
    void getCalendarResources(const QString &path,
                              const QDateTime &from, const QDateTime &to);
    void getCalendarResources(const QString &path, const QStringList &uids);
    void sendCalendarResource(const QString &path, const QString &data, const QString &etag = QString());

    void deleteResource(const QString &path);

signals:
    void dnsLookupFinished(const Reply &reply);
    void userPrincipalDataFinished(const Reply &reply);
    void calendarListFinished(const Reply &reply);
    void calendarEtagsFinished(const Reply &reply, const QHash<QString, QString> &etags);
    void calendarResourcesFinished(const Reply &reply, const QList<Resource> &resources);
    void sendCalendarFinished(const Reply &reply, const QString &etag);
    void deleteFinished(const Reply &reply);

private:
    QScopedPointer<ClientPrivate> d;
};
}
}

#endif
