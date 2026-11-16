#include "ModbusFrame.h"

#include "../common/Crc16.h"

namespace mb {
namespace frame {

namespace {
inline void appendU16BE(QByteArray& out, quint16 v)
{
    out.append(static_cast<char>((v >> 8) & 0xFF));
    out.append(static_cast<char>(v & 0xFF));
}
} // namespace

QByteArray buildReadPdu(Func func, quint16 startAddr, quint16 count)
{
    QByteArray pdu;
    pdu.reserve(5);
    pdu.append(static_cast<char>(static_cast<quint8>(func)));
    appendU16BE(pdu, startAddr);
    appendU16BE(pdu, count);
    return pdu;
}

QByteArray buildWriteSinglePdu(quint16 addr, quint16 value)
{
    QByteArray pdu;
    pdu.reserve(5);
    pdu.append(static_cast<char>(static_cast<quint8>(Func::WriteSingleReg)));
    appendU16BE(pdu, addr);
    appendU16BE(pdu, value);
    return pdu;
}

QByteArray wrapRtu(quint8 unitId, const QByteArray& pdu)
{
    QByteArray body;
    body.reserve(pdu.size() + 1);
    body.append(static_cast<char>(unitId));
    body.append(pdu);
    return rtuAppendCrc(body);
}

QByteArray wrapMbtcp(quint16 transactionId, quint8 unitId, const QByteArray& pdu)
{
    QByteArray out;
    out.reserve(kMbapSize + pdu.size());
    appendU16BE(out, transactionId);                 // 事务标识
    appendU16BE(out, kProtocolId);                   // 协议标识 = 0
    appendU16BE(out, static_cast<quint16>(pdu.size() + 1));  // 长度 = 单元标识 + PDU
    out.append(static_cast<char>(unitId));           // 单元标识
    out.append(pdu);
    return out;
}

bool isExceptionPdu(const QByteArray& pdu, quint8* exceptionCode)
{
    if (pdu.size() < 2)
        return false;
    const quint8 func = static_cast<quint8>(pdu.at(0));
    if ((func & 0x80) == 0)
        return false;
    if (exceptionCode)
        *exceptionCode = static_cast<quint8>(pdu.at(1));
    return true;
}

bool parseReadPdu(const QByteArray& pdu, Func expectedFunc,
                  QVector<quint16>& out, QString* error)
{
    out.clear();

    quint8 exCode = 0;
    if (isExceptionPdu(pdu, &exCode)) {
        if (error)
            *error = QStringLiteral("Modbus 异常响应: %1").arg(exceptionText(exCode));
        return false;
    }

    if (pdu.size() < 2) {
        if (error)
            *error = QStringLiteral("响应 PDU 过短 (%1 字节)").arg(pdu.size());
        return false;
    }

    const quint8 func = static_cast<quint8>(pdu.at(0));
    if (func != static_cast<quint8>(expectedFunc)) {
        if (error)
            *error = QStringLiteral("功能码不匹配: 期望 0x%1, 实际 0x%2")
                         .arg(static_cast<int>(expectedFunc), 2, 16, QLatin1Char('0'))
                         .arg(func, 2, 16, QLatin1Char('0'));
        return false;
    }

    const int byteCount = static_cast<quint8>(pdu.at(1));
    if (byteCount % 2 != 0) {
        if (error)
            *error = QStringLiteral("字节数不是偶数: %1").arg(byteCount);
        return false;
    }
    if (pdu.size() < 2 + byteCount) {
        if (error)
            *error = QStringLiteral("响应数据不足: 需要 %1 字节, 实际 %2")
                         .arg(byteCount).arg(pdu.size() - 2);
        return false;
    }

    out.resize(byteCount / 2);
    for (int i = 0; i < out.size(); ++i) {
        const quint8 hi = static_cast<quint8>(pdu.at(2 + i * 2));
        const quint8 lo = static_cast<quint8>(pdu.at(3 + i * 2));
        out[i] = static_cast<quint16>((hi << 8) | lo);
    }
    return true;
}

bool takeMbtcpFrame(QByteArray& rxBuffer, QByteArray& frameOut)
{
    if (rxBuffer.size() < 6)
        return false;

    const quint16 length =
        static_cast<quint16>((static_cast<quint8>(rxBuffer.at(4)) << 8) |
                             static_cast<quint8>(rxBuffer.at(5)));
    const int total = 6 + length;            // MBAP 前 6 字节 + 长度字段覆盖部分
    if (length < 2 || length > 260) {        // 长度非法，丢弃 1 字节重新对齐
        rxBuffer.remove(0, 1);
        return false;
    }
    if (rxBuffer.size() < total)
        return false;

    frameOut = rxBuffer.left(total);
    rxBuffer.remove(0, total);
    return true;
}

QString exceptionText(quint8 code)
{
    switch (static_cast<Exception>(code)) {
    case Exception::IllegalFunction:    return QStringLiteral("非法功能码");
    case Exception::IllegalDataAddress: return QStringLiteral("非法数据地址");
    case Exception::IllegalDataValue:   return QStringLiteral("非法数据值");
    case Exception::SlaveDeviceFailure: return QStringLiteral("从站设备故障");
    case Exception::Acknowledge:        return QStringLiteral("从站已接收，处理中");
    case Exception::SlaveDeviceBusy:    return QStringLiteral("从站忙");
    default:                            return QStringLiteral("未知异常码 0x%1")
                                                   .arg(code, 2, 16, QLatin1Char('0'));
    }
}

Func readFuncFor(bool holding)
{
    return holding ? Func::ReadHoldingRegs : Func::ReadInputRegs;
}

} // namespace frame
} // namespace mb
