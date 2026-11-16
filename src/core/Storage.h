#pragma once
// ============================================================================
//  SQLite 存储线程
//  ---------------------------------------------------------------------------
//  从有界队列批量取出采样点，在一个事务里批量 INSERT。
//  对比 “每条一次 INSERT”：逐条写入每次都要落盘、更新索引，
//  开销极大；事务批量写入把这些开销合并，吞吐可提升一个数量级。
//
//  注意：QSqlDatabase 的连接不能跨线程共享，
//  因此本类在自己的线程内用独立连接名创建连接。
// ============================================================================
#include "../common/Containers.h"
#include "../common/ModbusDefs.h"

#include <QString>

#include <atomic>
#include <mutex>
#include <thread>

namespace mb {

class Storage {
public:
    Storage();
    ~Storage();

    Storage(const Storage&)            = delete;
    Storage& operator=(const Storage&) = delete;

    bool open(const QString& dbPath, QString* error);
    void close();

    void setSource(BoundedQueue<DataPoint>* queue) { m_source = queue; }

    void start();
    void stop();

    //! 告警记录异步落库
    void saveAlarm(const Alarm& alarm);

    [[nodiscard]] quint64 inserted() const { return m_inserted.load(); }
    [[nodiscard]] QString lastError() const;

private:
    void run();
    bool prepareSchema(QString* error);
    void flushBatch(std::vector<DataPoint>& batch);
    void flushAlarms(std::vector<Alarm>& alarms);

    BoundedQueue<DataPoint>* m_source = nullptr;

    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<quint64> m_inserted{0};

    QString m_dbPath;
    QString m_connName;

    mutable std::mutex m_mutex;
    QString            m_lastError;

    BoundedQueue<Alarm> m_alarmQueue{512};
};

} // namespace mb
