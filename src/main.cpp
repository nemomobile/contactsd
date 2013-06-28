/** This file is part of Contacts daemon
 **
 ** Copyright (c) 2010-2011 Nokia Corporation and/or its subsidiary(-ies).
 **
 ** Contact:  Nokia Corporation (info@qt.nokia.com)
 **
 ** GNU Lesser General Public License Usage
 ** This file may be used under the terms of the GNU Lesser General Public License
 ** version 2.1 as published by the Free Software Foundation and appearing in the
 ** file LICENSE.LGPL included in the packaging of this file.  Please review the
 ** following information to ensure the GNU Lesser General Public License version
 ** 2.1 requirements will be met:
 ** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 **
 ** In addition, as a special exception, Nokia gives you certain additional rights.
 ** These rights are described in the Nokia Qt LGPL Exception version 1.1, included
 ** in the file LGPL_EXCEPTION.txt in this package.
 **
 ** Other Usage
 ** Alternatively, this file may be used in accordance with the terms and
 ** conditions contained in a signed written agreement between you and Nokia.
 **/

#include <QCoreApplication>
#include <QDBusConnection>
#include <QTimer>

#include <errno.h>
#include <signal.h>

#include "contactsd.h"
#include "debug.h"

using namespace Contactsd;

static QtMessageHandler defaultMsgHandler = 0;
static QtMsgType messageThreshold = QtWarningMsg;
static FILE *messageLog = NULL;

static void customMessageHandler(QtMsgType type, const QMessageLogContext &ctxt, const QString &msgStr)
{
    if (type < messageThreshold) {
        return; // no debug messages please
    }

    const QByteArray &msgData(msgStr.toLocal8Bit());
    const char *msg = msgData.constData();

    // Actually qInstallMsgHandler() returned null in main() when
    // I checked, so defaultMsgHandler should be null - but let's be careful.
    if (defaultMsgHandler) {
        defaultMsgHandler(type, ctxt, msgStr);
    } else {
        fprintf(stderr, "%s\n", msg);
    }

    if (messageLog) {
        if (fprintf(messageLog, "%s\n", msg) < 0 || fflush(messageLog) != 0) {
            const int messageLogError = errno;
            fclose(messageLog);
            messageLog = NULL;

            (warning().nospace()
                    << "An error occured when writing to the log file: "
                    << strerror(messageLogError) << " (" << messageLogError << "). "
                    << "The log file is truncated.").space();
        }
    }
}

static void usage()
{
    QTextStream(stdout)
            << "Usage: contactsd [OPTION]...\n"
            << "\n"
            << "Options:\n"
            << "\n"
            << "  --plugins PLUGINS    Comma separated list of plugins to load\n"
            << "  --log-console        Enable console logging\n"
            << "  --log-file FILENAME  Additional write logging information to FILENAME\n"
            << "  --version            Output version information and exit\n"
            << "  --help               Display this help and exit\n"
            << "\n";
}

static void setupUnixSignalHandlers()
{
    struct sigaction sigterm, sigint;

    sigterm.sa_handler = ContactsDaemon::unixSignalHandler;
    sigemptyset(&sigterm.sa_mask);
    sigterm.sa_flags = SA_RESTART;

    if (sigaction(SIGTERM, &sigterm, 0) < 0) {
        warning() << "Could not setup signal handler for SIGTERM";
        return;
    }

    sigint.sa_handler = ContactsDaemon::unixSignalHandler;
    sigemptyset(&sigint.sa_mask);
    sigint.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sigint, 0) < 0) {
        warning() << "Could not setup signal handler for SIGINT";
        return;
    }
}

int main(int argc, char **argv)
{
    setupUnixSignalHandlers();

    QCoreApplication app(argc, argv);

    QStringList plugins;
    bool logConsole = !qgetenv("CONTACTSD_DEBUG").isEmpty();
    QString logFileName;

    const QStringList args = app.arguments();
    int i = 1; // ignore argv[0]

    while (i < args.count()) {
        QString arg = args.at(i);

        if (arg == "--plugins") {
            if (++i == args.count()) {
                usage();
                return -1;
            }

            QString value = args.at(i);
            value.replace(" ", ",");
            plugins << value.split(",", QString::SkipEmptyParts);
        } else if (arg == "--version") {
            debug() << "contactsd version" << VERSION;
            return 0;
        } else if (arg == "--help") {
            usage();
            return 0;
        } else if (arg == "--log-console") {
            logConsole = true;
        } else if (arg == "--log-file") {
            if (++i == args.count()) {
                usage();
                return -1;
            }

            logFileName = args.at(i);
        } else {
            warning() << "Invalid argument" << arg;
            usage();
            return -1;
        }
        ++i;
    }

    if (logConsole) {
        messageThreshold = QtDebugMsg;
    }

    if (not logFileName.isEmpty()) {
        messageLog = fopen(qPrintable(logFileName), "w");
    }

    defaultMsgHandler = qInstallMessageHandler(customMessageHandler);

    enableDebug(logConsole);
    debug() << "contactsd version" << VERSION << "started";

    ContactsDaemon *daemon = new ContactsDaemon(&app);
    daemon->loadPlugins(plugins);

    const int rc = app.exec();

    if (messageLog) {
        fclose (messageLog);
    }

    return rc;
}
