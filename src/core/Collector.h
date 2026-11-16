#pragma once
// ============================================================================
//  采集线程
//  ---------------------------------------------------------------------------
//  工作流程（每轮）：
//    打开/重连 -> 逐个寄存器发 Modbus 读请求 -> 大端解析 -> 换算工程值
//    -> 写 “最近值快照”（给界面） -> 写 “历史环形缓冲”（给曲线/AI）
//    -> 写 “有界队列”（给存储线程批量落盘） -> 越限则产生告警
//
//  线程模型要点：
//    * 采集用独立的 std::thread 做阻塞式轮询，不依赖 Qt 事件循环；
//    * Modbus 客户端（内部含 QTcpSocket）在采集线程内创建与销毁，
//      避免 QTcpSocket 跨线程使用导致的 thread affinity 问题；
//    * 界面线程通过 snapshot()/takeNewAlarms() 轮询取数，不跨线程操作 QObject。
// ============================================================================
#include "../common/Containers.h"
#include "../common/ModbusDefs.h"
#include "../protocol/IModbusClient.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace mb {

class Collector {
public:
    Collector();
    ~Collector();

    Collector(const Collector&)            = delete;
    Collector& operator=(const Collector&) = delete;

    // ---------------------------------------------------------- 配置
    void setConfig(const CollectorConfig& cfg);   // 仅在停止状态下设置
    [[nodiscard]] CollectorConfig config() const;

    void setRegisters(const QVector<RegisterDef>& defs);
    [[nodiscard]] QVector<RegisterDef> registers() const;

    // ---------------------------------------------------------- 生命周期
    bool start(QString* error);
    void stop();
    [[nodiscard]] bool running() const { return m_running.load(); }

    // ---------------------------------------------------------- 数据出口
    [[nodiscard]] QVector<RegValue> snapshot() const;
    [[nodiscard]] CollectorStats stats() const;
    [[nodiscard]] QVector<Alarm> takeNewAlarms();

    //! 某寄存器最近 maxCount 个采样点（趋势曲线 / AI 工具调用）
    [[nodiscard]] std::vector<DataPoint> history(quint16 addr, int maxCount) const;
    [[nodiscard]] QVector<quint16> addresses() const;

    //! 存储线程消费的队列
    [[nodiscard]] BoundedQueue<DataPoint>& storeQueue() { return m_storeQueue; }

private:
    void run();
    bool pollOnce(IModbusClient* client);
    static std::unique_ptr<IModbusClient> createClient(const CollectorConfig& cfg);
    void handleAlarm(const RegisterDef& def, double value);
    void recordHistory(const DataPoint& p);
    void setError(const QString& err);

    // 配置
    mutable std::mutex   m_cfgMutex;
    CollectorConfig      m_config;
    QVector<RegisterDef> m_registers;

    // 线程
    std::thread       m_thread;
    std::atomic<bool> m_running{false};

    // 出口数据
    mutable std::mutex m_stateMutex;
    QVector<RegValue>  m_values;
    CollectorStats     m_stats;
    QVector<Alarm>     m_pendingAlarms;
    qint64             m_nextAlarmId = 1;

    // 每个地址一条历史缓冲（std::map 节点不移动，适配含 mutex 的 RingBuffer）
    mutable std::mutex                               m_historyMutex;
    mutable std::map<quint16, RingBuffer<DataPoint>> m_history;
    // 告警上升沿检测，避免同一次越限反复告警
    std::map<quint16, bool> m_alarmState;

    BoundedQueue<DataPoint> m_storeQueue{16384};
};

} // namespace mb
