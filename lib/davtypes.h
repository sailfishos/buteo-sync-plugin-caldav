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
#include <QFlags>

#include "davexport.h"

namespace Buteo {
namespace Dav {
enum Privilege {
    NO_PRIVILEGE = 0,
    READ = 1,
    WRITE = 2,
    WRITE_PROPERTIES = 4,
    WRITE_CONTENT = 8,
    UNLOCK = 16,
    READ_ACL = 32,
    READ_CURRENT_USER_SET = 64,
    WRITE_ACL = 128,
    BIND = 256,
    UNBIND = 512,
    ALL_PRIVILEGES = 1023
};
Q_DECLARE_FLAGS(Privileges, Privilege)
Q_DECLARE_OPERATORS_FOR_FLAGS(Privileges)

struct DAV_EXPORT CalendarInfo {
    QString remotePath;
    QString displayName;
    QString description;
    QString color;
    QString userPrincipal;
    Privileges privileges = READ | WRITE;
    bool allowEvents = true;
    bool allowTodos = true;
    bool allowJournals = true;

    CalendarInfo() {}
    CalendarInfo(const QString &path, const QString &name,
                 const QString &description, const QString &color,
                 const QString &principal = QString(),
                 Privileges privileges = READ | WRITE)
        : remotePath(path), displayName(name), description(description)
        , color(color), userPrincipal(principal), privileges(privileges) {}

    bool operator==(const CalendarInfo &other) const
    {
        return remotePath == other.remotePath
            && displayName == other.displayName
            && description == other.description
            && color == other.color
            && userPrincipal == other.userPrincipal
            && privileges == other.privileges
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
