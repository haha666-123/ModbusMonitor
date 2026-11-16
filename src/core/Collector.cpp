#include "Collector.h"

#include "../protocol/ModbusRtuClient.h"
#include "../protocol/ModbusTcpClient.h"

#include <QDateTime>
#include <QElapsedTimer>

#include <algorithm>
#include <chrono>

namespace mb {

Collector::Collector() = default;

Collector::~Collector()
{
    stop();
}

// ------------------------------------------------------------------ 配置
void Collector::setConfig(const CollectorConfig& cfg)
{
    std::scoped_lock lock(m_cfgMutex);
    m_config = cfg;
}

CollectorConfig Collector::config() const
{
    std::scoped_lock lock(m_cfgMutex);
    return m_config;
}

void Collector::setRegisters(const QVector<RegisterDef>& defs)
{
    {
        std::scoped_lock lock(m_cfgMutex);
        m_registers = defs;
    }
    std::scoped_lock hlock(m_historyMutex);
    m_history.clear();
}

QVector<RegisterDef> Collector::registers() const
{
    std::scoped_lock lock(m_cfgMutex);
    return m_registers;
}

QVector<quint16> Collector::addresses() const
{
    std::scoped_lock lock(m_cfgMutex);
    QVector<quint16> out;
    out.reserve(m_registers.size());
    for (const auto& d : m_registers)
        out.push_back(d.addr);
    return out;
}

// ------------------------------------------------------------------ 客户端工厂
std::unique_ptr<IModbusClient> Collector::createClient(const CollectorConfig& cfg)
{
    if (cfg.mode == LinkMode::Tcp)
        return std::make_unique<ModbusTcpClient>(cfg.host, cfg.port, cfg.timeoutMs);
    return std::make_unique<ModbusRtuClient>(cfg.serialPort, cfg.baudRate, cfg.timeoutMs);
}

// ------------------------------------------------------------------ 生命周期
bool Collector::start(QString* error)
{
    if (m_running.load())
        return true;

    {
        std::scoped_lock lock(m_cfgMutex);
        if (m_registers.isEmpty()) {
            if (error)
                *error = QStringLiteral("未配置任何寄存器");
            return false;
        }
    }

    m_storeQueue.reopen();
    m_running.store(true);
    m_thread = std::thread(&Collector::run, this);
    return true;
}

void Collector::stop()
{
    if (!m_running.exchange(false))
        return;
    if (m_thread.joinable())
        m_thread.join();
    m_storeQueue.close();

    std::scoped_lock lock(m_stateMutex);
    m_stats.connected = false;
}

// ------------------------------------------------------------------ 采集主循环
void Collector::run()
{
    // 注意：客户端在本线程内创建、使用、销毁，
    // 保证 QTcpSocket 的 thread affinity 始终在工作线程上。
    std::unique_ptr<IModbusClient> client;

    const CollectorConfig cfg = config();
    const int interval = std::max(20, cfg.pollIntervalMs);

    while (m_running.load()) {
        QElapsedTimer timer;
        timer.start();

        if (!client)
            client = createClient(cfg);

        if (!client->isOpen()) {
            QString err;
            if (!client->open(&err)) {
                setError(err);
                std::scoped_lock lock(m_stateMutex);
                ++m_stats.polls;
                ++m_stats.failed;
                m_stats.connected = false;
            }
        }

        if (client->isOpen())
            pollOnce(client.get());

        int remain = interval - static_cast<int>(timer.elapsed());
        while (remain > 0 && m_running.load()) {
            const int slice = std::min(remain, 20);
            std::this_thread::sleep_for(std::chrono::milliseconds(slice));
            remain -= slice;
        }
    }

    if (client)
        client->close();
    client.reset();
}

bool Collector::pollOnce(IModbusClient* client)
{
    const CollectorConfig cfg = config();
    const QVector<RegisterDef> defs = registers();

    if (!client || !client->isOpen()) {
        setError(QStringLiteral("链路未连接"));
        return false;
    }

    bool allOk = true;
    quint64 points = 0;
    QVector<RegValue> values;
    values.reserve(defs.size());

    QString err;

    for (const RegisterDef& def : defs) {
        QVector<quint16> regs;
        if (!client->readRegisters(cfg.unitId, Func::ReadHoldingRegs,
                                   def.addr, 1, regs, &err)) {
            allOk = false;
            setError(err);

            RegValue rv;
            rv.addr  = def.addr;
            rv.name  = def.name;
            rv.unit  = def.unit;
            rv.valid = false;
            values.push_back(rv);

            // 读失败通常意味着链路异常，本轮就此结束，下一轮重连
            break;
        }

        const quint16 raw = regs.isEmpty() ? 0 : regs.at(0);
        const double value = static_cast<double>(raw) * def.scale;

        bool alarmed = false;
        if (def.hasLimit) {
            const bool outOfRange = (value > def.highLimit) || (value < def.lowLimit);
            if (outOfRange) {
                alarmed = true;
                handleAlarm(def, value);
            } else {
                std::scoped_lock lock(m_stateMutex);
                m_alarmState[def.addr] = false;
            }
        }

        RegValue rv;
        rv.addr  = def.addr;
        rv.name  = def.name;
        rv.unit  = def.unit;
        rv.raw   = raw;
        rv.value = value;
        rv.valid = true;
        rv.alarm = alarmed;
        values.push_back(rv);

        DataPoint p;
        p.ts    = QDateTime::currentMSecsSinceEpoch();
        p.addr  = def.addr;
        p.raw   = raw;
        p.value = value;

        recordHistory(p);

        // 非阻塞写入：队列满时丢弃，保证采集不被磁盘 IO 拖慢
        m_storeQueue.tryPush(p);
        ++points;
    }

    {
        std::scoped_lock lock(m_stateMutex);
        m_values = values;
        ++m_stats.polls;
        if (allOk)
            ++m_stats.succeeded;
        else
            ++m_stats.failed;
        m_stats.points   += points;
        m_stats.connected = client->isOpen();
    }
    return allOk;
}

void Collector::handleAlarm(const RegisterDef& def, double value)
{
    std::scoped_lock lock(m_stateMutex);

    if (m_alarmState[def.addr])
        return;                       // 只在越限上升沿产生一条告警
    m_alarmState[def.addr] = true;

    Alarm a;
    a.id     = m_nextAlarmId++;
    a.ts     = QDateTime::currentMSecsSinceEpoch();
    a.addr   = def.addr;
    a.name   = def.name;
    a.unit   = def.unit;
    a.value  = value;
    a.isHigh = value > def.highLimit;
    a.limit  = a.isHigh ? def.highLimit : def.lowLimit;

    m_pendingAlarms.push_back(a);
    if (m_pendingAlarms.size() > 200)
        m_pendingAlarms.removeFirst();
}

void Collector::recordHistory(const DataPoint& p)
{
    std::scoped_lock lock(m_historyMutex);
    auto it = m_history.find(p.addr);
    if (it == m_history.end())
        it = m_history.try_emplace(p.addr, 300).first;
    it->second.push(p);
}

void Collector::setError(const QString& err)
{
    std::scoped_lock lock(m_stateMutex);
    m_stats.lastError = err;
}

// ------------------------------------------------------------------ 数据出口
QVector<RegValue> Collector::snapshot() const
{
    std::scoped_lock lock(m_stateMutex);
    return m_values;
}

CollectorStats Collector::stats() const
{
    std::scoped_lock lock(m_stateMutex);
    return m_stats;
}

QVector<Alarm> Collector::takeNewAlarms()
{
    std::scoped_lock lock(m_stateMutex);
    QVector<Alarm> out;
    out.swap(m_pendingAlarms);
    return out;
}

std::vector<DataPoint> Collector::history(quint16 addr, int maxCount) const
{
    std::scoped_lock lock(m_historyMutex);
    std::vector<DataPoint> out;
    auto it = m_history.find(addr);
    if (it == m_history.end())
        return out;

    out = it->second.snapshot();
    if (maxCount > 0 && static_cast<int>(out.size()) > maxCount)
        out.erase(out.begin(), out.end() - maxCount);
    return out;
}

} // namespace mb
