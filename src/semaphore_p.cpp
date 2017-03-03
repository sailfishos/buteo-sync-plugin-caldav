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

#include "semaphore_p.h"

#include <errno.h>
#include <unistd.h>

#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>

#include <QDebug>

namespace {

// Defined as required for ::semun
union semun {
    int              val;
    struct semid_ds *buf;
    unsigned short  *array;
    struct seminfo  *__buf;
};

void semaphoreError(const char *msg, const char *id, int error)
{
    qWarning() << "semaphore error:" << QString::fromLatin1("%1 %2: %3 (%4)").arg(msg).arg(id).arg(::strerror(error)).arg(error);
}

int semaphoreInit(const char *id, size_t count, const int *initialValues)
{
    int rv = -1;

    // It doesn't matter what proj_id we use, there are no other ftok uses on this ID
    key_t key = ::ftok(id, 2);

    rv = ::semget(key, count, 0);
    if (rv == -1) {
        if (errno != ENOENT) {
            semaphoreError("Unable to get semaphore", id, errno);
        } else {
            // The semaphore does not currently exist
            rv = ::semget(key, count, IPC_CREAT | IPC_EXCL | S_IRWXO | S_IRWXG | S_IRWXU);
            if (rv == -1) {
                if (errno == EEXIST) {
                    // Someone else won the race to create the semaphore - retry get
                    rv = ::semget(key, count, 0);
                }

                if (rv == -1) {
                    semaphoreError("Unable to create semaphore", id, errno);
                }
            } else {
                // Set the initial value
                for (size_t i = 0; i < count; ++i) {
                    union semun arg = { 0 };
                    arg.val = *initialValues++;

                    int status = ::semctl(rv, static_cast<int>(i), SETVAL, arg);
                    if (status == -1) {
                        rv = -1;
                        semaphoreError("Unable to initialize semaphore", id, errno);
                    }
                }
            }
        }
    }

    return rv;
}

bool semaphoreIncrement(int id, size_t index, bool wait, size_t ms, int value)
{
    if (id == -1) {
        errno = 0;
        return false;
    }

    struct sembuf op;
    op.sem_num = index;
    op.sem_op = value;
    op.sem_flg = SEM_UNDO;
    if (!wait) {
        op.sem_flg |= IPC_NOWAIT;
    }

    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = ms * 1000;

    do {
        int rv = ::semtimedop(id, &op, 1, (wait && ms > 0 ? &timeout : 0));
        if (rv == 0)
            return true;
    } while (errno == EINTR);

    return false;
}

static const int initialSemaphoreValues[] = { 1, 0, 1 };
static size_t fileOwnershipIndex = 0;
static size_t fileReadersIndex = 1;
static size_t writeAccessIndex = 2;

}

// Adapted from the inter-process mutex in QMF
// The first user creates the semaphore that all subsequent instances
// attach to.  We rely on undo semantics to release locked semaphores
// on process failure.
ProcessMutex::ProcessMutex(const QString &path)
    : mSemaphore(path.toLatin1(), 3, initialSemaphoreValues)
    , mInitialProcess(false)
{
    if (!mSemaphore.isValid()) {
        qWarning() << QStringLiteral("Unable to create semaphore array!");
    } else {
        if (!mSemaphore.decrement(fileOwnershipIndex)) {
            qWarning() << QStringLiteral("Unable to determine clean sync markers file ownership!");
        } else {
            // Only the first process to connect to the semaphore is the owner
            mInitialProcess = (mSemaphore.value(fileReadersIndex) == 0);
            if (!mSemaphore.increment(fileReadersIndex)) {
                qWarning() << QStringLiteral("Unable to increment clean sync markers file readers!");
            }

            mSemaphore.increment(fileOwnershipIndex);
        }
    }
}

bool ProcessMutex::lock()
{
    return mSemaphore.decrement(writeAccessIndex);
}

bool ProcessMutex::unlock()
{
    return mSemaphore.increment(writeAccessIndex);
}

bool ProcessMutex::isLocked() const
{
    return (mSemaphore.value(writeAccessIndex) == 0);
}

bool ProcessMutex::isInitialProcess() const
{
    return mInitialProcess;
}

ProcessMutex *ProcessMutex::instance(QScopedPointer<ProcessMutex> &processMutex, const QString &path)
{
    if (!processMutex) {
        processMutex.reset(new ProcessMutex(path));
    }
    return processMutex.data();
}

Semaphore::Semaphore(const char *id, int initial)
    : m_identifier(id)
    , m_id(-1)
{
    m_id = semaphoreInit(m_identifier.toUtf8().constData(), 1, &initial);
}

Semaphore::Semaphore(const char *id, size_t count, const int *initialValues)
    : m_identifier(id)
    , m_id(-1)
{
    m_id = semaphoreInit(m_identifier.toUtf8().constData(), count, initialValues);
}

Semaphore::~Semaphore()
{
}

bool Semaphore::isValid() const
{
    return (m_id != -1);
}

bool Semaphore::decrement(size_t index, bool wait, size_t timeoutMs)
{
    if (!semaphoreIncrement(m_id, index, wait, timeoutMs, -1)) {
        if (errno != EAGAIN || wait) {
            error("Unable to decrement semaphore", errno);
        }
        return false;
    }
    return true;
}

bool Semaphore::increment(size_t index, bool wait, size_t timeoutMs)
{
    if (!semaphoreIncrement(m_id, index, wait, timeoutMs, 1)) {
        if (errno != EAGAIN || wait) {
            error("Unable to increment semaphore", errno);
        }
        return false;
    }
    return true;
}

int Semaphore::value(size_t index) const
{
    if (m_id == -1)
        return -1;

    return ::semctl(m_id, index, GETVAL, 0);
}

void Semaphore::error(const char *msg, int error)
{
    semaphoreError(msg, m_identifier.toUtf8().constData(), error);
}

