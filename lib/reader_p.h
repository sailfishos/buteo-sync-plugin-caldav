/*
 * This file is part of buteo-sync-plugin-caldav package
 *
 * Copyright (C) 2013 Jolla Ltd. and/or its subsidiary(-ies).
 *
 * Contributors: Mani Chandrasekar <maninc@gmail.com>
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

#ifndef READER_H
#define READER_H

#include <QObject>

#include "davtypes.h"

class QXmlStreamReader;

class Reader : public QObject
{
    Q_OBJECT
public:
    explicit Reader(QObject *parent = nullptr);
    ~Reader();

    void read(const QByteArray &data);
    bool hasError() const;
    const QList<Buteo::Dav::Resource>& results() const;

private:
    void readMultiStatus();
    void readResponse();
    void readPropStat(Buteo::Dav::Resource *resource);
    void readProp(Buteo::Dav::Resource *resource);

private:
    QXmlStreamReader *mReader = nullptr;
    bool mValidResponse = false;
    QList<Buteo::Dav::Resource> mResults;
};

#endif // READER_H
