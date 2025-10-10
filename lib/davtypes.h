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

#ifndef DAVTYPES_H
#define DAVTYPES_H

#include <QString>
#include <QList>

#include "davexport.h"

namespace Buteo {
namespace Dav {
struct DAV_EXPORT CalendarInfo {
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
        , userPrincipal(principal), readOnly(readOnly) {}

    bool operator==(const CalendarInfo &other) const
    {
        return remotePath == other.remotePath
            && displayName == other.displayName
            && color == other.color
            && userPrincipal == other.userPrincipal
            && readOnly == other.readOnly
            && allowEvents == other.allowEvents
            && allowTodos == other.allowTodos
            && allowJournals == other.allowJournals;
    }
};

struct DAV_EXPORT Resource {
    QString href;
    QString etag;
    QString status;
    QString data;

    static QList<Resource> fromData(const QByteArray &data, bool *isOk = nullptr);
};
}
}

#endif
