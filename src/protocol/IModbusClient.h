#pragma once
// ============================================================================
//  Modbus 客户端抽象接口
//  ---------------------------------------------------------------------------
//  采集线程按 “请求-应答” 串行方式调用：一次只发一个请求，等应答回来再发下一个。
//  这样不需要处理多请求并发，RTU 的帧边界问题也自然解决。
// ============================================================================
#include "../common/ModbusDefs.h"

#include <QString>
#include <QVector>

namespace mb {

class IModbusClient {
public:
    virtual ~IModbusClient() = default;

    virtual bool open(QString* error)    = 0;
    virtual void close()                 = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;

    //! 同步读取 count 个寄存器（大端解析）
    virtual bool readRegisters(quint8 unitId,
                               Func func,
                               quint16 startAddr,
                               quint16 count,
                               QVector<quint16>& out,
                               QString* error) = 0;

    //! 人类可读的连接描述，用于界面状态栏
    [[nodiscard]] virtual QString describe() const = 0;
};

} // namespace mb
