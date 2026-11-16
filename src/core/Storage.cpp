#include "Storage.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <chrono>

namespace mb {

Storage::Storage()
{
    static std::atomic<int> counter{0};
    m_connName = QStringLiteral("mb_storage_%1").arg(counter.fetch_add(1));
}

Storage::~Storage()
{
    stop();
    close();
}

QString Storage::lastError() const
{
    std::scoped_lock lock(m_mutex);
    return m_lastError;
}

bool Storage::open(const QString& dbPath, QString* error)
{
    if (dbPath.isEmpty()) {
        if (error) *error = QStringLiteral("数据库路径为空");
        return false;
    }

    const QFileInfo fi(dbPath);
    QDir dir = fi.absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (error) *error = QStringLiteral("无法创建目录: %1").arg(dir.absolutePath());
        return false;
    }

    m_dbPath = fi.absoluteFilePath();
    return true;
}

void Storage::close()
{
    if (!m_dbPath.isEmpty()) {
        if (QSqlDatabase::contains(m_connName))
            QSqlDatabase::removeDatabase(m_connName);
    }
}

void Storage::start()
{
    if (m_running.load())
        return;
    m_running.store(true);
    m_thread = std::thread(&Storage::run, this);
}

void Storage::stop()
{
    if (!m_running.exchange(false))
        return;
    m_alarmQueue.close();
    if (m_thread.joinable())
        m_thread.join();
}

void Storage::saveAlarm(const Alarm& alarm)
{
    m_alarmQueue.tryPush(alarm);
}

bool Storage::prepareSchema(QString* error)
{
    QSqlQuery q(QSqlDatabase::database(m_connName));

    if (!q.exec(QStringLiteral("PRAGMA journal_mode=WAL"))) {
        if (error) *error = q.lastError().text();
        return false;
    }
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

    const QStringList ddl = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS datapoint ("
            "  id    INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  ts    INTEGER NOT NULL,"
            "  addr  INTEGER NOT NULL,"
            "  raw   INTEGER NOT NULL,"
            "  value REAL    NOT NULL)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_dp_ts ON datapoint(ts)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_dp_addr_ts ON datapoint(addr, ts)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS alarm ("
            "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  ts         INTEGER NOT NULL,"
            "  addr       INTEGER NOT NULL,"
            "  name       TEXT,"
            "  value      REAL,"
            "  limit_val  REAL,"
            "  is_high    INTEGER,"
            "  diagnosis  TEXT)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_alarm_ts ON alarm(ts)")
    };

    for (const QString& sql : ddl) {
        if (!q.exec(sql)) {
            if (error) *error = q.lastError().text();
            return false;
        }
    }
    return true;
}

void Storage::run()
{
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
        db.setDatabaseName(m_dbPath);
        if (!db.open()) {
            std::scoped_lock lock(m_mutex);
            m_lastError = QStringLiteral("打开数据库失败: %1").arg(db.lastError().text());
            m_running.store(false);
            return;
        }
        QString err;
        if (!prepareSchema(&err)) {
            std::scoped_lock lock(m_mutex);
            m_lastError = QStringLiteral("初始化表结构失败: %1").arg(err);
        }
    }

    std::vector<DataPoint> batch;
    batch.reserve(512);
    std::vector<Alarm> alarms;
    alarms.reserve(64);

    while (m_running.load()) {
        batch.clear();
        if (m_source)
            m_source->popBatch(batch, 512, 500);

        if (!batch.empty())
            flushBatch(batch);

        alarms.clear();
        Alarm a;
        while (m_alarmQueue.pop(a, 0))
            alarms.push_back(a);
        if (!alarms.empty())
            flushAlarms(alarms);
    }

    // 收尾：把队列里剩余的数据落盘
    if (m_source) {
        batch.clear();
        while (m_source->popBatch(batch, 512, 0) > 0) {
            flushBatch(batch);
            batch.clear();
        }
    }

    {
        QSqlDatabase db = QSqlDatabase::database(m_connName, false);
        if (db.isOpen())
            db.close();
    }
}

void Storage::flushBatch(std::vector<DataPoint>& batch)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    if (!db.isOpen())
        return;

    if (!db.transaction()) {
        std::scoped_lock lock(m_mutex);
        m_lastError = QStringLiteral("开启事务失败: %1").arg(db.lastError().text());
        return;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO datapoint (ts, addr, raw, value) VALUES (?, ?, ?, ?)"));

    bool ok = true;
    for (const DataPoint& p : batch) {
        q.addBindValue(static_cast<qlonglong>(p.ts));
        q.addBindValue(static_cast<int>(p.addr));
        q.addBindValue(static_cast<int>(p.raw));
        q.addBindValue(p.value);
        if (!q.exec()) {
            ok = false;
            std::scoped_lock lock(m_mutex);
            m_lastError = QStringLiteral("写入采样点失败: %1").arg(q.lastError().text());
            break;
        }
        q.finish();
    }

    if (!ok) {
        db.rollback();
        return;
    }

    if (!db.commit()) {
        std::scoped_lock lock(m_mutex);
        m_lastError = QStringLiteral("提交事务失败: %1").arg(db.lastError().text());
        db.rollback();
        return;
    }

    m_inserted.fetch_add(batch.size());
}

void Storage::flushAlarms(std::vector<Alarm>& alarms)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    if (!db.isOpen())
        return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO alarm (ts, addr, name, value, limit_val, is_high, diagnosis) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));

    for (const Alarm& a : alarms) {
        q.addBindValue(static_cast<qlonglong>(a.ts));
        q.addBindValue(static_cast<int>(a.addr));
        q.addBindValue(a.name);
        q.addBindValue(a.value);
        q.addBindValue(a.limit);
        q.addBindValue(a.isHigh ? 1 : 0);
        q.addBindValue(a.diagnosis);
        q.exec();
        q.finish();
    }
}

} // namespace mb
