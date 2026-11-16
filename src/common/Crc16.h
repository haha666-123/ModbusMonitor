#pragma once
// ============================================================================
//  Modbus CRC16
//  ---------------------------------------------------------------------------
//  算法: 多项式 0xA001 (0x8005 反序), 初值 0xFFFF, 输入输出均不反转
//  RTU 帧中 CRC 按 “低字节在前” 追加
// ============================================================================
#include <QtGlobal>
#include <QByteArray>

namespace mb {

inline quint16 crc16(const quint8* data, int len)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < len; ++i) {
        crc ^= static_cast<quint16>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001)
                crc = static_cast<quint16>((crc >> 1) ^ 0xA001);
            else
                crc = static_cast<quint16>(crc >> 1);
        }
    }
    return crc;
}

inline quint16 crc16(const QByteArray& data)
{
    return crc16(reinterpret_cast<const quint8*>(data.constData()), data.size());
}

//! 在帧尾追加 CRC16 (低字节在前)
inline QByteArray rtuAppendCrc(const QByteArray& body)
{
    const quint16 c = crc16(body);
    QByteArray out = body;
    out.append(static_cast<char>(c & 0xFF));
    out.append(static_cast<char>((c >> 8) & 0xFF));
    return out;
}

//! 校验 RTU 帧尾部 CRC
inline bool rtuCheckCrc(const QByteArray& frame)
{
    if (frame.size() < 4)
        return false;
    QByteArray body = frame.left(frame.size() - 2);
    const quint16 expect = crc16(body);
    const quint8 lo = static_cast<quint8>(frame.at(frame.size() - 2));
    const quint8 hi = static_cast<quint8>(frame.at(frame.size() - 1));
    const quint16 actual = static_cast<quint16>(lo | (hi << 8));
    return expect == actual;
}

} // namespace mb
