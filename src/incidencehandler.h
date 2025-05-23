/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2014 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Bea Lam <bea.lam@jollamobile.com>
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
#ifndef INCIDENCEHANDLER_P_H
#define INCIDENCEHANDLER_P_H

#include <KCalendarCore/Incidence>
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/Journal>
#include <KCalendarCore/Attendee>

class IncidenceHandler
{
public:
    static QString toIcs(const KCalendarCore::Incidence::Ptr incidence,
                         const KCalendarCore::Incidence::List instances = KCalendarCore::Incidence::List());

private:
    IncidenceHandler();
    ~IncidenceHandler();

    static KCalendarCore::Incidence::Ptr incidenceToExport(KCalendarCore::Incidence::Ptr sourceIncidence,
                                                           const KCalendarCore::Incidence::List &instances = KCalendarCore::Incidence::List());

    friend class tst_NotebookSyncAgent;
    friend class tst_IncidenceHandler;
};

#endif // INCIDENCEHANDLER_P_H
