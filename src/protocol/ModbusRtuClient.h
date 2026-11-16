#pragma once
// ============================================================================
//  Modbus RTU 客户端（手写协议栈，基于 SerialPort）
//  ---------------------------------------------------------------------------
//  RTU 帧: 从站地址(1) + PDU + CRC16(2, 低字节在前)
//  串口没有 “消息边界”，协议靠 ≥3.5 字符时间的静默间隔区分帧。
//  本实现采用 “一问一答” 模型：发出请求后按预期长度收齐响应，
//  只要不足预期长度就继续等，直到超时，从而避免半包问题。
// ============================================================================
#include "IModbusClient.h"
#include "SerialPort.h"

#include <QString>

namespace mb {

class ModbusRtuClient : public IModbusClient {
public:
    ModbusRtuClient(QString portName, int baudRate, int timeoutMs);

    bool open(QString* error) override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;

    bool readRegisters(quint8 unitId, Func func, quint16 startAddr, quint16 count,
                       QVector<quint16>& out, QString* error) override;

    [[nodiscard]] QString describe() const override;

private:
    SerialPort m_port;
    QString    m_portName;
    int        m_baudRate  = 9600;
    int        m_timeoutMs = 800;
};

} // namespace mb
