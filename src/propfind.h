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
    explicit PropFind(QNetworkAccessManager *manager, Settings *settings, QObject *parent = 0);

    void listCalendars(const QString &calendarsPath);
    const QList<Settings::CalendarInfo>& calendars() const;

private Q_SLOTS:
    void processResponse();

private:
    void sendRequest(const QString &remotePath, const QByteArray &requestData);
    QList<Settings::CalendarInfo> mCalendars;
};

#endif
