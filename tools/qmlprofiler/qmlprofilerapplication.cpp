/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtQml module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qmlprofilerapplication.h"
#include "constants.h"
#include <QtCore/QStringList>
#include <QtCore/QTextStream>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <QtCore/QDebug>
#include <QtCore/QCommandLineParser>

static const char commandTextC[] =
        "You can control the recoding interactively with the "
        "following commands:\n"
        "    r, record\n"
        "        Switch recording on or off.\n"
        "    q, quit\n"
        "        Terminate program.";

static const char TraceFileExtension[] = ".qtd";

QmlProfilerApplication::QmlProfilerApplication(int &argc, char **argv) :
    QCoreApplication(argc, argv),
    m_runMode(LaunchMode),
    m_process(0),
    m_tracePrefix(QLatin1String("trace")),
    m_hostName(QLatin1String("127.0.0.1")),
    m_port(3768),
    m_verbose(false),
    m_quitAfterSave(false),
    m_recording(true),
    m_qmlProfilerClient(&m_connection),
    m_v8profilerClient(&m_connection),
    m_connectionAttempts(0),
    m_qmlDataReady(false),
    m_v8DataReady(false)
{
    m_connectTimer.setInterval(1000);
    connect(&m_connectTimer, SIGNAL(timeout()), this, SLOT(tryToConnect()));

    connect(&m_connection, SIGNAL(connected()), this, SLOT(connected()));
    connect(&m_connection, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this, SLOT(connectionStateChanged(QAbstractSocket::SocketState)));
    connect(&m_connection, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(connectionError(QAbstractSocket::SocketError)));

    connect(&m_qmlProfilerClient, SIGNAL(enabledChanged()), this, SLOT(traceClientEnabled()));
    connect(&m_qmlProfilerClient, SIGNAL(range(QQmlProfilerService::RangeType,QQmlProfilerService::BindingType,qint64,qint64,QStringList,QmlEventLocation)),
            &m_profilerData, SLOT(addQmlEvent(QQmlProfilerService::RangeType,QQmlProfilerService::BindingType,qint64,qint64,QStringList,QmlEventLocation)));
    connect(&m_qmlProfilerClient, SIGNAL(traceFinished(qint64)), &m_profilerData, SLOT(setTraceEndTime(qint64)));
    connect(&m_qmlProfilerClient, SIGNAL(traceStarted(qint64)), &m_profilerData, SLOT(setTraceStartTime(qint64)));
    connect(&m_qmlProfilerClient, SIGNAL(traceStarted(qint64)), this, SLOT(notifyTraceStarted()));
    connect(&m_qmlProfilerClient, SIGNAL(frame(qint64,int,int,int)), &m_profilerData, SLOT(addFrameEvent(qint64,int,int,int)));
    connect(&m_qmlProfilerClient, SIGNAL(sceneGraphFrame(QQmlProfilerService::SceneGraphFrameType,
                                         qint64,qint64,qint64,qint64,qint64,qint64)),
            &m_profilerData, SLOT(addSceneGraphFrameEvent(QQmlProfilerService::SceneGraphFrameType,
                                  qint64,qint64,qint64,qint64,qint64,qint64)));
    connect(&m_qmlProfilerClient, SIGNAL(pixmapCache(QQmlProfilerService::PixmapEventType,qint64,
                                                     QmlEventLocation,int,int,int)),
            &m_profilerData, SLOT(addPixmapCacheEvent(QQmlProfilerService::PixmapEventType,qint64,
                                                      QmlEventLocation,int,int,int)));
    connect(&m_qmlProfilerClient, SIGNAL(memoryAllocation(QQmlProfilerService::MemoryType,qint64,
                                                          qint64)),
            &m_profilerData, SLOT(addMemoryEvent(QQmlProfilerService::MemoryType,qint64,
                                                 qint64)));

    connect(&m_qmlProfilerClient, SIGNAL(complete()), this, SLOT(qmlComplete()));

    connect(&m_v8profilerClient, SIGNAL(enabledChanged()), this, SLOT(profilerClientEnabled()));
    connect(&m_v8profilerClient, SIGNAL(range(int,QString,QString,int,double,double)),
            &m_profilerData, SLOT(addV8Event(int,QString,QString,int,double,double)));
    connect(&m_v8profilerClient, SIGNAL(complete()), this, SLOT(v8Complete()));

    connect(&m_profilerData, SIGNAL(error(QString)), this, SLOT(logError(QString)));
    connect(&m_profilerData, SIGNAL(dataReady()), this, SLOT(traceFinished()));

}

QmlProfilerApplication::~QmlProfilerApplication()
{
    if (!m_process)
        return;
    logStatus("Terminating process ...");
    m_process->disconnect();
    m_process->terminate();
    if (!m_process->waitForFinished(1000)) {
        logStatus("Killing process ...");
        m_process->kill();
    }
    delete m_process;
}

void QmlProfilerApplication::parseArguments()
{
    setApplicationName(QLatin1String("qmlprofiler"));
    setApplicationVersion(QLatin1String(qVersion()));

    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);

    parser.setApplicationDescription(QChar::LineFeed + tr(
        "The QML Profiler retrieves QML tracing data from an application. The data\n"
        "collected can then be visualized in Qt Creator. The application to be profiled\n"
        "has to enable QML debugging. See the Qt Creator documentation on how to do\n"
        "this for different Qt versions.") + QChar::LineFeed + QChar::LineFeed + tr(commandTextC));

    QCommandLineOption attach(QStringList() << QLatin1String("a") << QLatin1String("attach"),
                              tr("Attach to an application already running on <hostname>, "
                                 "instead of starting it locally."),
                              QLatin1String("hostname"));
    parser.addOption(attach);

    QCommandLineOption port(QStringList() << QLatin1String("p") << QLatin1String("port"),
                            tr("Connect to the TCP port <port>. The default is 3768."),
                            QLatin1String("port"), QLatin1String("3768"));
    parser.addOption(port);

    QCommandLineOption record(QLatin1String("record"),
                              tr("If set to 'off', don't immediately start recording data when the "
                                 "QML engine starts, but instead either start the recording "
                                 "interactively or with the JavaScript console.profile() function. "
                                 "By default the recording starts immediately."),
                              QLatin1String("on|off"), QLatin1String("on"));
    parser.addOption(record);

    QCommandLineOption verbose(QStringList() << QLatin1String("verbose"),
                               tr("Print debugging output."));
    parser.addOption(verbose);

    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument(QLatin1String("program"),
                                 tr("The program to be started and profiled."),
                                 QLatin1String("[program]"));
    parser.addPositionalArgument(QLatin1String("parameters"),
                                 tr("Parameters for the program to be started."),
                                 QLatin1String("[parameters...]"));

    parser.process(*this);

    if (parser.isSet(attach)) {
        m_hostName = parser.value(attach);
        m_runMode = AttachMode;
    }

    if (parser.isSet(port)) {
        bool isNumber;
        m_port = parser.value(port).toUShort(&isNumber);
        if (!isNumber) {
            logError(tr("'%1' is not a valid port.").arg(parser.value(port)));
            parser.showHelp(1);
        }
    }

    m_recording = (parser.value(record) == QLatin1String("on"));

    if (parser.isSet(verbose))
        m_verbose = true;

    m_programArguments = parser.positionalArguments();
    if (!m_programArguments.isEmpty()) {
        m_programPath = m_programArguments.takeFirst();
        m_tracePrefix = QFileInfo(m_programPath).fileName();
    }

    if (m_runMode == LaunchMode && m_programPath.isEmpty()) {
        logError(tr("You have to specify either --attach or a program to start."));
        parser.showHelp(2);
    }

    if (m_runMode == AttachMode && !m_programPath.isEmpty()) {
        logError(tr("--attach cannot be used when starting a program."));
        parser.showHelp(3);
    }
}

int QmlProfilerApplication::exec()
{
    QTimer::singleShot(0, this, SLOT(run()));
    return QCoreApplication::exec();
}

void QmlProfilerApplication::printCommands()
{
    print(tr(commandTextC));
}

QString QmlProfilerApplication::traceFileName() const
{
    QString fileName = m_tracePrefix + "_" +
            QDateTime::currentDateTime().toString(QLatin1String("yyMMdd_hhmmss")) +
            TraceFileExtension;
    if (QFileInfo(fileName).exists()) {
        QString baseName;
        int suffixIndex = 0;
        do {
            baseName = QFileInfo(fileName).baseName()
                    + QString::number(suffixIndex++);
        } while (QFileInfo(baseName + TraceFileExtension).exists());
        fileName = baseName + TraceFileExtension;
    }

    return QFileInfo(fileName).absoluteFilePath();
}

void QmlProfilerApplication::userCommand(const QString &command)
{
    QString cmd = command.trimmed();
    if (cmd == Constants::CMD_HELP
            || cmd == Constants::CMD_HELP2
            || cmd == Constants::CMD_HELP3) {
        printCommands();
    } else if (cmd == Constants::CMD_RECORD
               || cmd == Constants::CMD_RECORD2) {
        m_qmlProfilerClient.sendRecordingStatus(!m_recording);
        m_v8profilerClient.sendRecordingStatus(!m_recording);
    } else if (cmd == Constants::CMD_QUIT
               || cmd == Constants::CMD_QUIT2) {
        print(QLatin1String("Quit"));
        if (m_recording) {
            m_quitAfterSave = true;
            m_qmlProfilerClient.sendRecordingStatus(false);
            m_v8profilerClient.sendRecordingStatus(false);
        } else {
            quit();
        }
    }
}

void QmlProfilerApplication::notifyTraceStarted()
{
    // Synchronize to server state. It doesn't hurt to do this multiple times in a row for
    // different traces. There is no symmetric event to "Complete" after all.
    m_recording = true;
}

void QmlProfilerApplication::run()
{
    if (m_runMode == LaunchMode) {
        m_process = new QProcess(this);
        QStringList arguments;
        arguments << QString::fromLatin1("-qmljsdebugger=port:%1,block").arg(m_port);
        arguments << m_programArguments;

        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, SIGNAL(readyRead()), this, SLOT(processHasOutput()));
        connect(m_process, SIGNAL(finished(int,QProcess::ExitStatus)), this,
                SLOT(processFinished()));
        logStatus(QString("Starting '%1 %2' ...").arg(m_programPath,
                                                      arguments.join(" ")));
        m_process->start(m_programPath, arguments);
        if (!m_process->waitForStarted()) {
            logError(QString("Could not run '%1': %2").arg(m_programPath,
                                                           m_process->errorString()));
            exit(1);
        }

    }
    m_connectTimer.start();
}

void QmlProfilerApplication::tryToConnect()
{
    Q_ASSERT(!m_connection.isConnected());
    ++ m_connectionAttempts;

    if (!m_verbose && !(m_connectionAttempts % 5)) {// print every 5 seconds
        if (!m_verbose)
            logError(QString("Could not connect to %1:%2 for %3 seconds ...").arg(
                         m_hostName, QString::number(m_port),
                         QString::number(m_connectionAttempts)));
    }

    if (m_connection.state() == QAbstractSocket::UnconnectedState) {
        logStatus(QString("Connecting to %1:%2 ...").arg(m_hostName,
                                                         QString::number(m_port)));
        m_connection.connectToHost(m_hostName, m_port);
    }
}

void QmlProfilerApplication::connected()
{
    m_connectTimer.stop();
    print(tr("Connected to host:port %1:%2. Wait for profile data or type a command (type 'help' "
             "to show list of commands).").arg(m_hostName).arg((m_port)));
    print(tr("Recording Status: %1").arg(m_recording ? tr("on") : tr("off")));
}

void QmlProfilerApplication::connectionStateChanged(
        QAbstractSocket::SocketState state)
{
    if (m_verbose)
        qDebug() << state;
}

void QmlProfilerApplication::connectionError(QAbstractSocket::SocketError error)
{
    if (m_verbose)
        qDebug() << error;
}

void QmlProfilerApplication::processHasOutput()
{
    Q_ASSERT(m_process);
    while (m_process->bytesAvailable()) {
        QTextStream out(stdout);
        out << m_process->readAll();
    }
}

void QmlProfilerApplication::processFinished()
{
    Q_ASSERT(m_process);
    if (m_process->exitStatus() == QProcess::NormalExit) {
        logStatus(QString("Process exited (%1).").arg(m_process->exitCode()));

        if (m_recording) {
            logError("Process exited while recording, last trace is lost!");
            exit(2);
        } else {
            exit(0);
        }
    } else {
        logError("Process crashed! Exiting ...");
        exit(3);
    }
}

void QmlProfilerApplication::traceClientEnabled()
{
    logStatus("Trace client is attached.");
    // blocked server is waiting for recording message from both clients
    // once the last one is connected, both messages should be sent
    m_qmlProfilerClient.sendRecordingStatus(m_recording);
    m_v8profilerClient.sendRecordingStatus(m_recording);
}

void QmlProfilerApplication::profilerClientEnabled()
{
    logStatus("Profiler client is attached.");

    // blocked server is waiting for recording message from both clients
    // once the last one is connected, both messages should be sent
    m_qmlProfilerClient.sendRecordingStatus(m_recording);
    m_v8profilerClient.sendRecordingStatus(m_recording);
}

void QmlProfilerApplication::traceFinished()
{
    m_recording = false; // only on "Complete" we know that the trace is really finished.
    const QString fileName = traceFileName();

    if (m_profilerData.save(fileName))
        print(QString("Saving trace to %1.").arg(fileName));

    // after saving, reset the flags
    m_qmlDataReady = false;
    m_v8DataReady = false;

    if (m_quitAfterSave)
        quit();
}

void QmlProfilerApplication::print(const QString &line)
{
    QTextStream err(stderr);
    err << line << endl;
}

void QmlProfilerApplication::logError(const QString &error)
{
    QTextStream err(stderr);
    err << "Error: " << error << endl;
}

void QmlProfilerApplication::logStatus(const QString &status)
{
    if (!m_verbose)
        return;
    QTextStream err(stderr);
    err << status << endl;
}

void QmlProfilerApplication::qmlComplete()
{
    m_qmlDataReady = true;
    if (m_v8profilerClient.state() != QQmlDebugClient::Enabled ||
            m_v8DataReady) {
        m_profilerData.complete();
    }
}

void QmlProfilerApplication::v8Complete()
{
    m_v8DataReady = true;
    if (m_qmlProfilerClient.state() != QQmlDebugClient::Enabled ||
            m_qmlDataReady) {
        m_profilerData.complete();
    }
}
