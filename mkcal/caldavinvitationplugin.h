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

#ifndef CALDAVINVITATIONPLUGIN_H
#define CALDAVINVITATIONPLUGIN_H

// KCalendarCore
#include <invitationhandlerif.h>

// mKCal
#include <servicehandlerif.h>

#include <QtCore/QObject>

using namespace KCalendarCore;

class CalDAVInvitationPluginPrivate;
class CalDAVInvitationPlugin : public QObject, public InvitationHandlerInterface, public ServiceInterface
{
    Q_OBJECT
    Q_INTERFACES(InvitationHandlerInterface)
    Q_INTERFACES(ServiceInterface)
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.mkcal.CalDAVInvitationHandlerInterface")

public:
    CalDAVInvitationPlugin();
    ~CalDAVInvitationPlugin();

    // InvitationHandler
    bool sendInvitation(const QString &accountId,
                        const QString &notebookId,
                        const Incidence::Ptr &invitation,
                        const QString &body) override;
    bool sendUpdate(const QString &accountId,
                    const Incidence::Ptr &invitation,
                    const QString &body) override;
    bool sendResponse(const QString &accountId,
                      const Incidence::Ptr &invitation,
                      const QString &body) override;
    QString pluginName() const override;

    // ServiceHandler
    QString uiName() const override;
    QString icon() const override;
    bool multiCalendar() const override;
    QString emailAddress(const mKCal::Notebook::Ptr &notebook) override;
    QString displayName(const mKCal::Notebook::Ptr &notebook) const override;
    bool downloadAttachment(const mKCal::Notebook::Ptr &notebook,
                            const QString &uri,
                            const QString &path) override;
    bool deleteAttachment(const mKCal::Notebook::Ptr &notebook,
                          const Incidence::Ptr &incidence,
                          const QString &uri) override;
    bool shareNotebook(const mKCal::Notebook::Ptr &notebook,
                       const QStringList &sharedWith) override;
    QStringList sharedWith(const mKCal::Notebook::Ptr &notebook) override;
    QString serviceName() const override;
    QString defaultNotebook() const override;
    bool checkProductId(const QString &productId) const override;
    ErrorCode error() const override;

private:
    Q_DISABLE_COPY(CalDAVInvitationPlugin)
    CalDAVInvitationPluginPrivate * const d;
};

#endif
