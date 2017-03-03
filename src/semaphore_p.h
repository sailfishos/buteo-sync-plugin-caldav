/*
 * Copyright (C) 2017 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Matt Vogt <matthew.vogt@jollamobile.com>
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

#ifndef BUTEO_SYNC_PLUGIN_CALDAV_SEMAPHORE_P_H
#define BUTEO_SYNC_PLUGIN_CALDAV_SEMAPHORE_P_H

#include <QString>
#include <QScopedPointer>

class Semaphore
{
public:
    Semaphore(const char *identifier, int initial);
    Semaphore(const char *identifier, size_t count, const int *initialValues);
    ~Semaphore();

    bool isValid() const;

    bool decrement(size_t index = 0, bool wait = true, size_t timeoutMs = 0);
    bool increment(size_t index = 0, bool wait = true, size_t timeoutMs = 0);

    int value(size_t index = 0) const;

private:
    void error(const char *msg, int error);

    QString m_identifier;
    int m_id;
};

class ProcessMutex
{
    Semaphore mSemaphore;
    bool mInitialProcess;

public:
    bool lock();
    bool unlock();
    bool isLocked() const;
    bool isInitialProcess() const;

    // caller has e.g., mutable QScopedPointer<ProcessMutex> mProcessMutex.
    static ProcessMutex *instance(QScopedPointer<ProcessMutex> &processMutex, const QString &path);

private:
    explicit ProcessMutex(const QString &path);
};

#endif // BUTEO_SYNC_PLUGIN_CALDAV_SEMAPHORE_P_H
