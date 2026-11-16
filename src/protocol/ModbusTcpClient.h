#pragma once
// ============================================================================
//  Modbus TCP 客户端（手写协议栈，基于 QTcpSocket 阻塞式收发）
//  ---------------------------------------------------------------------------
//  运行在采集线程中，不使用事件循环，全部采用 waitForXxx 同步等待，
//  一次请求-应答完成后再发下一次，因此不存在多请求乱序问题。
// ============================================================================
#include "IModbusClient.h"

#include <QByteArray>
#include <QString>
#include <QTcpSocket>

namespace mb {

class ModbusTcpClient : public IModbusClient {
public:
    ModbusTcpClient(QString host, quint16 port, int timeoutMs);

    bool open(QString* error) override;
    void close() override;
    [[nodiscard]] bool isOpen() const override;

    bool readRegisters(quint8 unitId, Func func, quint16 startAddr, quint16 count,
                       QVector<quint16>& out, QString* error) override;

    [[nodiscard]] QString describe() const override;

private:
    bool ensureConnected(QString* error);

    QTcpSocket m_socket;
    QString    m_host;
    quint16    m_port       = 502;
    int        m_timeoutMs  = 800;
    quint16    m_transId    = 0;
    QByteArray m_rxBuffer;
};

} // namespace mb
