#include "ModbusRtuClient.h"

#include "../common/Crc16.h"
#include "ModbusFrame.h"

namespace mb {

ModbusRtuClient::ModbusRtuClient(QString portName, int baudRate, int timeoutMs)
    : m_portName(std::move(portName)), m_baudRate(baudRate), m_timeoutMs(timeoutMs)
{
}

QString ModbusRtuClient::describe() const
{
    return QStringLiteral("Modbus RTU %1 @%2").arg(m_portName).arg(m_baudRate);
}

bool ModbusRtuClient::isOpen() const
{
    return m_port.isOpen();
}

bool ModbusRtuClient::open(QString* error)
{
    if (m_port.isOpen())
        return true;
    return m_port.open(m_portName, m_baudRate, error);
}

void ModbusRtuClient::close()
{
    m_port.close();
}

bool ModbusRtuClient::readRegisters(quint8 unitId, Func func,
                                    quint16 startAddr, quint16 count,
                                    QVector<quint16>& out, QString* error)
{
    out.clear();

    if (count == 0 || count > kMaxReadRegs) {
        if (error)
            *error = QStringLiteral("寄存器数量非法: %1").arg(count);
        return false;
    }

    if (!m_port.isOpen()) {
        if (!open(error))
            return false;
    }

    // 清掉上一次可能残留的字节，避免错帧
    m_port.flushInput();

    const QByteArray request = frame::wrapRtu(unitId, frame::buildReadPdu(func, startAddr, count));
    if (m_port.write(request) != request.size()) {
        if (error)
            *error = QStringLiteral("串口写入失败: %1").arg(m_port.lastError());
        return false;
    }

    // 正常响应: 地址(1)+功能码(1)+字节数(1)+数据(2N)+CRC(2) = 5 + 2N
    // 异常响应: 地址(1)+功能码|0x80(1)+异常码(1)+CRC(2)      = 5
    const int normalLen = 5 + count * 2;
    const int exceptionLen = 5;

    QByteArray rx;
    QByteArray chunk = m_port.read(normalLen, m_timeoutMs);
    rx.append(chunk);

    if (rx.isEmpty()) {
        if (error)
            *error = QStringLiteral("RTU 读取超时（无任何字节返回）");
        return false;
    }

    // 先判断是否异常响应
    const bool isException = (rx.size() >= 2) &&
                             (static_cast<quint8>(rx.at(1)) & 0x80);

    const int wantLen = isException ? exceptionLen : normalLen;

    // 继续收齐剩余字节
    if (rx.size() < wantLen) {
        const QByteArray more = m_port.read(wantLen - rx.size(), m_timeoutMs);
        rx.append(more);
    }

    if (rx.size() < wantLen) {
        if (error)
            *error = QStringLiteral("RTU 响应长度不足: 期望 %1 字节，实际 %2 字节")
                         .arg(wantLen).arg(rx.size());
        return false;
    }

    const QByteArray respFrame = rx.left(wantLen);

    if (!rtuCheckCrc(respFrame)) {
        if (error)
            *error = QStringLiteral("RTU CRC 校验失败");
        return false;
    }

    const quint8 respUnit = static_cast<quint8>(respFrame.at(0));
    if (respUnit != unitId) {
        if (error)
            *error = QStringLiteral("从站地址不匹配: 期望 %1, 实际 %2").arg(unitId).arg(respUnit);
        return false;
    }

    // 去掉地址(1) 与 CRC(2)，剩下的是 PDU
    const QByteArray pdu = respFrame.mid(1, respFrame.size() - 3);

    if (isException) {
        quint8 exCode = 0;
        frame::isExceptionPdu(pdu, &exCode);
        if (error)
            *error = QStringLiteral("Modbus 异常响应: %1").arg(frame::exceptionText(exCode));
        return false;
    }

    return frame::parseReadPdu(pdu, func, out, error);
}

} // namespace mb
