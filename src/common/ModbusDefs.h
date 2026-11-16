#pragma once
// ============================================================================
//  Modbus 基础类型定义
// ============================================================================
#include <QtGlobal>
#include <QString>
#include <QVector>
#include <QMetaType>

namespace mb {

// ---------------------------------------------------------------- 功能码
enum class Func : quint8 {
    ReadCoils          = 0x01,
    ReadDiscreteInputs = 0x02,
    ReadHoldingRegs    = 0x03,   // 读保持寄存器 (4x, 可读写)
    ReadInputRegs      = 0x04,   // 读输入寄存器 (3x, 只读)
    WriteSingleReg     = 0x06,
    WriteMultipleRegs  = 0x10,
};

// ---------------------------------------------------------------- 异常码
enum class Exception : quint8 {
    None                = 0x00,
    IllegalFunction     = 0x01,
    IllegalDataAddress  = 0x02,
    IllegalDataValue    = 0x03,
    SlaveDeviceFailure  = 0x04,
    Acknowledge         = 0x05,
    SlaveDeviceBusy     = 0x06,
};

// 单次最多读取的寄存器数量 (协议规定 0x7D = 125)
constexpr int kMaxReadRegs = 125;
// Modbus TCP 默认端口
constexpr quint16 kModbusTcpPort = 502;

// ---------------------------------------------------------------- 寄存器定义
struct RegisterDef {
    quint16 addr      = 0;        // 寄存器起始地址
    QString name;                 // 显示名，如 “温度”
    QString unit;                 // 单位，如 “℃”
    double  scale     = 1.0;      // 原始值 × scale = 工程值
    double  lowLimit  = 0.0;
    double  highLimit = 0.0;
    bool    hasLimit  = false;    // 是否启用上下限报警
};

// ---------------------------------------------------------------- 数据点
struct DataPoint {
    qint64  ts    = 0;    // 毫秒时间戳
    quint16 addr  = 0;
    quint16 raw   = 0;
    double  value = 0.0;  // 工程值
};

// ---------------------------------------------------------------- 告警
struct Alarm {
    qint64  id      = 0;
    qint64  ts      = 0;
    quint16 addr    = 0;
    QString name;
    QString unit;
    double  value   = 0.0;
    double  limit   = 0.0;
    bool    isHigh  = true;     // true=超上限, false=低于下限
    QString diagnosis;          // AI 诊断结果（异步填充）
    bool    diagnosisReady = false;
};

// ---------------------------------------------------------------- 界面用的实时值
struct RegValue {
    quint16 addr   = 0;
    QString name;
    QString unit;
    quint16 raw    = 0;
    double  value  = 0.0;
    bool    valid  = false;
    bool    alarm  = false;
};

// ---------------------------------------------------------------- 采集配置
enum class LinkMode { Tcp, Rtu };

struct CollectorConfig {
    LinkMode mode           = LinkMode::Tcp;
    QString  host           = QStringLiteral("127.0.0.1");
    quint16  port           = 5020;
    QString  serialPort     = QStringLiteral("COM3");
    int      baudRate       = 9600;
    quint8   unitId         = 1;
    int      pollIntervalMs = 500;
    int      timeoutMs      = 800;
    bool     autoReconnect  = true;
};

// ---------------------------------------------------------------- 统计
struct CollectorStats {
    quint64 polls     = 0;
    quint64 succeeded = 0;
    quint64 failed    = 0;
    quint64 points    = 0;
    bool    connected = false;
    QString lastError;
};

} // namespace mb

Q_DECLARE_METATYPE(mb::DataPoint)
