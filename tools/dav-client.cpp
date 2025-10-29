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

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>

#include <davclient.h>

static QString privilegesToString(Buteo::Dav::Privileges privileges)
{
    QStringList set;
    if (privileges & Buteo::Dav::READ)
        set.append(QStringLiteral("READ"));
    if (privileges & Buteo::Dav::WRITE)
        set.append(QStringLiteral("WRITE"));
    if (privileges & Buteo::Dav::WRITE_PROPERTIES)
        set.append(QStringLiteral("WRITE_PROPERTIES"));
    if (privileges & Buteo::Dav::UNLOCK)
        set.append(QStringLiteral("UNLOCK"));
    if (privileges & Buteo::Dav::READ_ACL)
        set.append(QStringLiteral("READ_ACL"));
    if (privileges & Buteo::Dav::READ_CURRENT_USER_SET)
        set.append(QStringLiteral("READ_CURRENT_USER_SET"));
    if (privileges & Buteo::Dav::WRITE_ACL)
        set.append(QStringLiteral("WRITE_ACL"));
    if (privileges & Buteo::Dav::BIND)
        set.append(QStringLiteral("BIND"));
    if (privileges & Buteo::Dav::UNBIND)
        set.append(QStringLiteral("UNBIND"));
    if (privileges & Buteo::Dav::READ
        && privileges & Buteo::Dav::WRITE
        && privileges & Buteo::Dav::WRITE_PROPERTIES
        && privileges & Buteo::Dav::UNLOCK
        && privileges & Buteo::Dav::READ_ACL
        && privileges & Buteo::Dav::READ_CURRENT_USER_SET
        && privileges & Buteo::Dav::WRITE_ACL
        && privileges & Buteo::Dav::BIND
        && privileges & Buteo::Dav::UNBIND) {
        set.clear();
        set.append(QStringLiteral("ALL_PRIVILEGES"));
    }
    if (set.isEmpty())
        set.append(QStringLiteral("NO_PRIVILEGE"));
    return QString::fromLatin1("{%1}").arg(set.join(QStringLiteral(", ")));
}

class DavCli : public QCoreApplication
{
public:
    DavCli(int argc, char *argv[])
        : QCoreApplication(argc, argv)
        , mFrom(QDateTime::currentDateTimeUtc().addDays(-7))
        , mTo(QDateTime::currentDateTimeUtc().addDays(+7))
    {
        setApplicationName("dav-client");

        mParser.setApplicationDescription("Command line tool to execute DAV requests with a server.");
        mParser.addHelpOption();

        mParser.addOption(QCommandLineOption(QStringList() << "s" << "server",
                                            "server address (like https://dav.example.org/).", "server"));
        mParser.addOption(QCommandLineOption(QStringList() << "R" << "root",
                                            "DAV root path.", "path"));
        mParser.addOption(QCommandLineOption(QStringList() << "ignore-ssl-errors",
                                            "ignore SSL errors and continue."));
        mParser.addOption(QCommandLineOption(QStringList() << "S" << "service",
                                            "DAV specific service.", "service"));
        mParser.addOption(QCommandLineOption(QStringList() << "u" << "user",
                                            "authenticate by username.", "login"));
        mParser.addOption(QCommandLineOption(QStringList() << "P" << "password",
                                            "authenticate with a password.", "passwd"));
        mParser.addOption(QCommandLineOption(QStringList() << "T" << "token",
                                            "authenticate with a token.", "token"));

        mParser.addOption(QCommandLineOption(QStringList()  << "list-calendars",
                                             "list available calendars for the auhenticated user.", "path"));

        mParser.addOption(QCommandLineOption(QStringList() << "list-calendar-etags",
                                             "list etags of all calendar resources in range (default is one week before, and one week after).", "path"));

        mParser.addOption(QCommandLineOption(QStringList() << "get-calendar-resources",
                                             "get all calendar resources in time range or by uri list.", "path"));

        mParser.addOption(QCommandLineOption(QStringList() << "i" << "uri",
                                             "identifier for a resource", "path"));

        mParser.addOption(QCommandLineOption(QStringList() << "f" << "from",
                                             "provide a starting date for range inquiries", "date"));
        mParser.addOption(QCommandLineOption(QStringList() << "t" << "to",
                                             "provide an ending date for range inquiries", "date"));

        mParser.addOption(QCommandLineOption(QStringList() << "p" << "put",
                                             "send a resource on server (etag should match to update, and left empty when new).", "path:file:etag"));

        mParser.addOption(QCommandLineOption(QStringList() << "d" << "delete",
                                             "delete a resource.", "path"));


        mParser.process(*this);

        mDAV = new Buteo::Dav::Client(mParser.value("s"), this);

        if (mParser.isSet("u") && mParser.isSet("P")) {
            mDAV->setAuthLogin(mParser.value("u"), mParser.value("P"));
        } else if (mParser.isSet("T")) {
            mDAV->setAuthToken(mParser.value("T"));
        }

        if (mParser.isSet("ignore-ssl-errors"))
            mDAV->setIgnoreSSLErrors(true);

        if (mParser.isSet("f"))
            mFrom = QDateTime::fromString(mParser.value("f"), Qt::ISODate);
        if (mParser.isSet("t"))
            mTo = QDateTime::fromString(mParser.value("t"), Qt::ISODate);

        connect(mDAV, &Buteo::Dav::Client::userPrincipalDataFinished,
                this, &DavCli::onUserPrincipalDataFinisdhed);
        mDAV->requestUserPrincipalAndServiceData(mParser.value("S"),
                                                 mParser.value("R"));
    }

    void execute(const QString &except = QString())
    {
        if (mParser.isSet("list-calendars") && except != "list-calendars") {
            connect(mDAV, &Buteo::Dav::Client::calendarListFinished, this, &DavCli::onCalendarListFinished);
            mDAV->requestCalendarList(mParser.value("list-calendars"));
        } else if (mParser.isSet("list-calendar-etags") && except != "list-calendar-etags") {
            connect(mDAV, &Buteo::Dav::Client::calendarEtagsFinished, this, &DavCli::onCalendarEtagsFinished);
            mDAV->getCalendarEtags(mParser.value("list-calendar-etags"), mFrom, mTo);
        } else if (mParser.isSet("get-calendar-resources") && except != "get-calendar-resources") {
            connect(mDAV, &Buteo::Dav::Client::calendarResourcesFinished, this, &DavCli::onCalendarResourcesFinished);
            if (!mParser.isSet("i")) {
                mDAV->getCalendarResources(mParser.value("get-calendar-resources"), mFrom, mTo);
            } else {
                mDAV->getCalendarResources(mParser.value("get-calendar-resources"), mParser.values("i"));
            }
        } else if (mParser.isSet("p") && except != "p") {
            connect(mDAV, &Buteo::Dav::Client::sendCalendarFinished, this, &DavCli::onSendCalendarFinished);
            const QStringList parts = mParser.value("p").split(":");
            if (parts.length() < 2) {
                qWarning() << "wrong put format. Awaited path:filename:etag.";
                exit(1);
            }
            QFile ics(parts[1]);
            if (!ics.open(QIODevice::ReadOnly | QIODevice::Text)) {
                qWarning() << "cannot read ICS data from" << parts[1];
                exit(1);
            }
            if (parts.length() == 2) {
                mDAV->sendCalendarResource(parts[0], ics.readAll());
            } else if (parts.length() == 3) {
                mDAV->sendCalendarResource(parts[0], ics.readAll(), parts[2]);
            }
        } else if (mParser.isSet("d") && except != "d") {
            connect(mDAV, &Buteo::Dav::Client::deleteFinished, this, &DavCli::onDeleteFinished);
            mDAV->deleteResource(mParser.value("d"));
        } else {
            exit(0);
        }
    }

    void onUserPrincipalDataFinisdhed(const Buteo::Dav::Client::Reply &reply)
    {
        if (reply.hasError()) {
            qWarning() << reply.errorMessage;
            qWarning() << reply.errorData;
        } else {
            qInfo() << "DAV resources:";
            qInfo() << "  server:" << mDAV->serverAddress();
            qInfo() << "  user principal:" << mDAV->userPrincipal();
            qInfo() << "  services:";
            for (const QString &service : mDAV->services()) {
                qInfo().noquote() << QString::fromLatin1("  - %1:").arg(service);
                qInfo() << "      email:" << mDAV->serviceMailto(service);
                qInfo() << "      path:" << mDAV->servicePath(service);
            }
        }

        execute();
    }

    void onCalendarListFinished(const Buteo::Dav::Client::Reply &reply)
    {
        if (reply.hasError()) {
            qWarning() << reply.errorMessage;
            qWarning() << reply.errorData;
        } else {
            qInfo() << "  calendars:";
            for (const Buteo::Dav::CalendarInfo &cal : mDAV->calendars()) {
                qInfo() << "  - label:" << cal.displayName;
                qInfo() << "    path:" << cal.remotePath;
                qInfo() << "    color:" << cal.color;
                qInfo() << "    user:" << cal.userPrincipal;
                qInfo().noquote() << "    privileges:" << privilegesToString(cal.privileges);
                qInfo() << "    allow events:" << (cal.allowEvents ? "yes" : "no");
                qInfo() << "    allow todos:" << (cal.allowTodos ? "yes" : "no");
                qInfo() << "    allow journals:" << (cal.allowJournals ? "yes" : "no");
            }
        }

        execute("list-calendars");
    }

    void onCalendarEtagsFinished(const Buteo::Dav::Client::Reply &reply,
                                 const QHash<QString, QString> &etags)
    {
        if (reply.hasError()) {
            qWarning() << reply.errorMessage;
            qWarning() << reply.errorData;
        } else {
            qInfo() << "  etags:";
            for (QHash<QString, QString>::ConstIterator it = etags.constBegin();
                 it != etags.constEnd(); it++) {
                qInfo() << "  - href:" << it.key();
                qInfo() << "    etag:" << it.value();
            }
        }

        execute("list-calendar-etags");
    }

    void onCalendarResourcesFinished(const Buteo::Dav::Client::Reply &reply,
                                     const QList<Buteo::Dav::Resource> &resources)
    {
        if (reply.hasError()) {
            qWarning() << reply.errorMessage;
            qWarning() << reply.errorData;
        } else {
            qInfo() << "  resources:" << (resources.isEmpty() ? "[]" : "");
            for (const Buteo::Dav::Resource &resource : resources) {
                qInfo() << "  - href:" << resource.href;
                qInfo() << "    etag:" << resource.etag;
                qInfo() << "    status:" << resource.status;
                qInfo() << "    data:" << resource.data;
            }
        }

        execute("get-calendar-resources");
    }

    void onSendCalendarFinished(const Buteo::Dav::Client::Reply &reply, const QString &etag)
    {
        if (reply.hasError()) {
            qWarning() << reply.errorMessage;
            qWarning() << reply.errorData;
        } else {
            qInfo() << "  put:";
            qInfo() << "  - href:" << reply.uri;
            qInfo() << "  - etag:" << etag;
        }

        execute("p");
    }

    void onDeleteFinished(const Buteo::Dav::Client::Reply &reply)
    {
        if (reply.hasError()) {
            qWarning() << reply.errorMessage;
            qWarning() << reply.errorData;
        } else {
            qInfo() << "  delete:";
            qInfo() << "  - href:" << reply.uri;
        }

        execute("d");
    }

    QCommandLineParser mParser;
    Buteo::Dav::Client *mDAV;
    QDateTime mFrom, mTo;
};

int main(int argc, char *argv[])
{
    DavCli app(argc, argv);

    return app.exec();
}
