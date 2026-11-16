#include "ModbusTcpClient.h"

#include "ModbusFrame.h"

#include <QElapsedTimer>
#include <QHostAddress>

namespace mb {

ModbusTcpClient::ModbusTcpClient(QString host, quint16 port, int timeoutMs)
    : m_host(std::move(host)), m_port(port), m_timeoutMs(timeoutMs)
{
}

QString ModbusTcpClient::describe() const
{
    return QStringLiteral("Modbus TCP %1:%2").arg(m_host).arg(m_port);
}

bool ModbusTcpClient::isOpen() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

bool ModbusTcpClient::ensureConnected(QString* error)
{
    if (isOpen())
        return true;

    m_socket.abort();
    m_rxBuffer.clear();

    m_socket.connectToHost(m_host, m_port);
    if (!m_socket.waitForConnected(m_timeoutMs)) {
        if (error) {
            *error = QStringLiteral("连接 %1:%2 失败: %3")
                         .arg(m_host).arg(m_port).arg(m_socket.errorString());
        }
        m_socket.abort();
        return false;
    }
    return true;
}

bool ModbusTcpClient::open(QString* error)
{
    return ensureConnected(error);
}

void ModbusTcpClient::close()
{
    m_rxBuffer.clear();
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.disconnectFromHost();
        if (m_socket.state() != QAbstractSocket::UnconnectedState)
            m_socket.waitForDisconnected(200);
        m_socket.abort();
    }
}

bool ModbusTcpClient::readRegisters(quint8 unitId, Func func,
                                    quint16 startAddr, quint16 count,
                                    QVector<quint16>& out, QString* error)
{
    out.clear();

    if (count == 0 || count > kMaxReadRegs) {
        if (error)
            *error = QStringLiteral("寄存器数量非法: %1 (允许 1..%2)").arg(count).arg(kMaxReadRegs);
        return false;
    }

    if (!ensureConnected(error))
        return false;

    const quint16 tid = ++m_transId;
    const QByteArray request = frame::wrapMbtcp(tid, unitId,
                                                frame::buildReadPdu(func, startAddr, count));

    if (m_socket.write(request) != request.size()) {
        if (error)
            *error = QStringLiteral("发送请求失败: %1").arg(m_socket.errorString());
        m_socket.abort();
        return false;
    }
    if (!m_socket.waitForBytesWritten(m_timeoutMs)) {
        if (error)
            *error = QStringLiteral("写入超时");
        m_socket.abort();
        return false;
    }

    QElapsedTimer timer;
    timer.start();

    while (true) {
        // 先从已有缓冲区里尝试切帧
        QByteArray frameBytes;
        while (frame::takeMbtcpFrame(m_rxBuffer, frameBytes)) {
            if (frameBytes.size() < frame::kMbapSize) {
                continue;
            }

            const quint16 respTid =
                static_cast<quint16>((static_cast<quint8>(frameBytes.at(0)) << 8) |
                                     static_cast<quint8>(frameBytes.at(1)));
            const quint16 protoId =
                static_cast<quint16>((static_cast<quint8>(frameBytes.at(2)) << 8) |
                                     static_cast<quint8>(frameBytes.at(3)));
            const quint8 respUnit = static_cast<quint8>(frameBytes.at(6));

            if (protoId != frame::kProtocolId)
                continue;                       // 协议标识错误，丢弃
            if (respTid != tid)
                continue;                       // 过期应答（超时重发后的旧包）
            if (respUnit != unitId)
                continue;                       // 从站不符

            const QByteArray pdu = frameBytes.mid(frame::kMbapSize);
            return frame::parseReadPdu(pdu, func, out, error);
        }

        const int remain = m_timeoutMs - static_cast<int>(timer.elapsed());
        if (remain <= 0) {
            if (error)
                *error = QStringLiteral("读取响应超时 (%1 ms)").arg(m_timeoutMs);
            m_socket.abort();
            m_rxBuffer.clear();
            return false;
        }

        if (!m_socket.waitForReadyRead(remain)) {
            if (m_socket.state() != QAbstractSocket::ConnectedState) {
                if (error)
                    *error = QStringLiteral("连接被对端关闭: %1").arg(m_socket.errorString());
                m_socket.abort();
                m_rxBuffer.clear();
                return false;
            }
            // 只是这一轮没数据，继续等（下一轮会处理超时）
            continue;
        }

        m_rxBuffer.append(m_socket.readAll());
    }
}

} // namespace mb
