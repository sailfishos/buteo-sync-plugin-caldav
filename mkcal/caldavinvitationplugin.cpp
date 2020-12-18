/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2019 - 2020 Open Mobile Platform LLC
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

#include "caldavinvitationplugin.h"

#include <KCalendarCore/ICalFormat>

#include <extendedcalendar.h>

#include <QDebug>

using namespace KCalendarCore;

class CalDAVInvitationPluginPrivate
{
public:
    ServiceInterface::ErrorCode m_errorCode = ServiceInterface::ErrorNotSupported;
};


CalDAVInvitationPlugin::CalDAVInvitationPlugin()
    : d(new CalDAVInvitationPluginPrivate)
{
}

CalDAVInvitationPlugin::~CalDAVInvitationPlugin()
{
    delete d;
}

bool CalDAVInvitationPlugin::sendInvitation(
        const QString &,
        const QString &,
        const Incidence::Ptr &,
        const QString &)
{
    d->m_errorCode = ServiceInterface::ErrorNotSupported;
    return false;
}

bool CalDAVInvitationPlugin::sendUpdate(
        const QString &,
        const Incidence::Ptr &,
        const QString &)
{
    d->m_errorCode = ServiceInterface::ErrorNotSupported;
    return false;
}

bool CalDAVInvitationPlugin::sendResponse(
        const QString &,
        const Incidence::Ptr &,
        const QString &)
{
    d->m_errorCode = ServiceInterface::ErrorNotSupported;
    return false;
}

QString CalDAVInvitationPlugin::pluginName() const
{
    d->m_errorCode = ServiceInterface::ErrorOk;
    return QLatin1String("caldav");
}

QString CalDAVInvitationPlugin::uiName() const
{
    d->m_errorCode = ServiceInterface::ErrorOk;
    return QLatin1String("CalDAV");
}

QString CalDAVInvitationPlugin::icon() const
{
    d->m_errorCode = ServiceInterface::ErrorOk;
    return QString();
}

bool CalDAVInvitationPlugin::multiCalendar() const
{
    d->m_errorCode = ServiceInterface::ErrorNotSupported;
    return false;
}

static const QByteArray EMAIL_PROPERTY = QByteArrayLiteral("userPrincipalEmail");
QString CalDAVInvitationPlugin::emailAddress(
        const mKCal::Notebook::Ptr &notebook)
{
    d->m_errorCode = ServiceInterface::ErrorOk;
    return notebook->customProperty(EMAIL_PROPERTY);
}

QString CalDAVInvitationPlugin::displayName(
        const mKCal::Notebook::Ptr &) const
{
    d->m_errorCode = ServiceInterface::ErrorNotSupported;
    return QString();
}

bool CalDAVInvitationPlugin::downloadAttachment(
        const mKCal::Notebook::Ptr &,
        const QString &,
        const QString &)
{
    d->m_errorCode = ServiceInterface::ErrorNotSupported;
    return false;
}

bool CalDAVInvitationPlugin::deleteAttachment(
        const mKCal::Notebook::Ptr &,
        const Incidence::Ptr &,
        const QString &)
{
    d->m_errorCode = ServiceInterface::ErrorNotSupported;
    return false;
}

bool CalDAVInvitationPlugin::shareNotebook(
        const mKCal::Notebook::Ptr &,
        const QStringList &)
{
    d->m_errorCode = ServiceInterface::ErrorNotSupported;
    return false;
}

QStringList CalDAVInvitationPlugin::sharedWith(
        const mKCal::Notebook::Ptr &)
{
    d->m_errorCode = ServiceInterface::ErrorNotSupported;
    return QStringList();
}

QString CalDAVInvitationPlugin::CalDAVInvitationPlugin::serviceName() const
{
    return pluginName();
}


QString CalDAVInvitationPlugin::defaultNotebook() const
{
    d->m_errorCode = ServiceInterface::ErrorNotSupported;
    return QString();
}

bool CalDAVInvitationPlugin::checkProductId(
        const QString &) const
{
    d->m_errorCode = ServiceInterface::ErrorNotSupported;
    return false;
}

ServiceInterface::ErrorCode CalDAVInvitationPlugin::error() const
{
    return d->m_errorCode;
}
