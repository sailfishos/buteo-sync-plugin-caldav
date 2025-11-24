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

#ifndef HEAD_H
#define HEAD_H

#include "request_p.h"

#include <QUrl>

class QNetworkAccessManager;
class Settings;

class Head : public Request
{
    Q_OBJECT

public:
    Head(QNetworkAccessManager *manager, Settings *settings, QObject *parent = nullptr);

    void getServiceUrl(const QString &service);
    QUrl serviceUrl(const QString &service) const;

protected:
    virtual void handleReply(QNetworkReply *reply);

private:
    enum HeadRequestType {
        ServiceUrl
    };

    void sendRequest(const QString &remotePath, HeadRequestType type);

    HeadRequestType mType;
    QMap<QString, QUrl> mServiceUrls;
};

#endif
