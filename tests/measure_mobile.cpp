#include <QGuiApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QDebug>
#include <QUuid>
#include <algorithm>
#include <cstdio>
#include "app/mobile_app_controller.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

template<typename Operation>
void measure(const char *name, Operation operation) {
    QList<qint64> samples;
    for (int i = 0; i < 7; ++i) {
        QElapsedTimer clock;
        clock.start();
        operation();
        samples.append(clock.nsecsElapsed() / 1000);
    }
    std::sort(samples.begin(), samples.end());
    qInfo().noquote() << name << "median_us=" << samples.at(3);
}

int main(int argc, char **argv) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        std::fprintf(stdout, "%s\n", message.toUtf8().constData());
        std::fflush(stdout);
    });
    QGuiApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("Synsemble Synthetic Measurements");
    QCoreApplication::setApplicationName(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QStringList ids;
    {
        amt::DatabaseManager db;
        if (!db.initialize()) return 1;
        for (int table = 0; table < 5; ++table) {
            amt::SessionState state;
            state.tableId = QString("fixture-%1").arg(table);
            state.title = "Synthetic performance fixture";
            state.updatedAt = QDateTime::currentDateTimeUtc();
            ids.append(state.tableId);
            for (int i = 0; i < 2000; ++i) {
                amt::TranscriptEntry entry;
                entry.tableId = state.tableId;
                entry.entryId = state.tableId + QString("-message-%1").arg(i);
                entry.speakerName = "Fixture agent";
                entry.content = QString(256, QLatin1Char('x'));
                entry.timestamp = state.updatedAt.addSecs(i);
                state.transcript.append(entry);
                amt::LogEvent event;
                event.tableId = state.tableId;
                event.logId = state.tableId + QString("-event-%1").arg(i);
                event.summary = "Synthetic completed operation";
                event.timestamp = entry.timestamp;
                state.log.append(event);
            }
            if (!db.saveTable(state)) return 2;
        }
    }
    measure("restore_5_tables_10000_messages", [] { amt::MobileAppController controller; if (!controller.initialize()) qFatal("Fixture restore failed"); });
    amt::MobileAppController controller;
    if (!controller.initialize()) return 3;
    measure("switch_5_tables", [&] { for (const auto &id : ids) controller.selectTable(id); });
    measure("transcript_2000_rows", [&] { if (controller.transcript().size() != 2000) qFatal("Missing transcript rows"); });
    measure("activity_2000_rows", [&] { if (controller.logs().size() != 2000) qFatal("Missing Activity rows"); });
    measure("flush_2000_rows", [&] { if (!controller.flushCurrentSession()) qFatal("Fixture flush failed"); });
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        qInfo() << "working_set_bytes=" << counters.WorkingSetSize;
#endif
    // All records belong to this unique synthetic namespace.
    for (const auto &id : ids) { controller.selectTable(id); if (!controller.deleteCurrentTable()) return 4; }
    return 0;
}
