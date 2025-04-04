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

#ifndef PROPFIND_H
#define PROPFIND_H

#include "request.h"

class QNetworkAccessManager;
class Settings;

class PropFind : public Request
{
    Q_OBJECT

public:
    struct CalendarInfo {
        QString remotePath;
        QString displayName;
        QString color;
        QString userPrincipal;
        bool readOnly = false;
        bool allowEvents = true;
        bool allowTodos = true;
        bool allowJournals = true;

        CalendarInfo() {}
        CalendarInfo(const QString &path, const QString &name, const QString &color,
                     const QString &principal = QString(), bool readOnly = false)
            : remotePath(path), displayName(name), color(color)
            , userPrincipal(principal), readOnly(readOnly)
        {}

        bool operator==(const CalendarInfo &other) const
        {
            return (remotePath == other.remotePath
                    && displayName == other.displayName
                    && color == other.color
                    && userPrincipal == other.userPrincipal
                    && readOnly == other.readOnly
                    && allowEvents == other.allowEvents
                    && allowTodos == other.allowTodos
                    && allowJournals == other.allowJournals);
        }
    };

    explicit PropFind(QNetworkAccessManager *manager, Settings *settings, QObject *parent = 0);

    void listCurrentUserPrincipal();
    QString userPrincipal() const;

    void listUserAddressSet(const QString &userPrincipal);
    QString userMailtoHref() const;
    QString userHomeHref() const;

    void listCalendars(const QString &calendarsPath);
    const QList<CalendarInfo>& calendars() const;

protected:
    virtual void handleReply(QNetworkReply *reply);

private:
    enum PropFindRequestType {
        UserPrincipal,
        UserAddressSet,
        ListCalendars
    };

    void sendRequest(const QString &remotePath, const QByteArray &requestData, PropFindRequestType reqType);
    bool parseUserPrincipalResponse(const QByteArray &data);
    bool parseUserAddressSetResponse(const QByteArray &data);
    bool parseCalendarResponse(const QByteArray &data);

    QList<CalendarInfo> mCalendars;
    QString mUserPrincipal;
    QString mUserMailtoHref;
    QString mUserHomeHref;
    PropFindRequestType mPropFindRequestType = UserPrincipal;

    friend class tst_Propfind;
};

#endif
