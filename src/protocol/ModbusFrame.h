#pragma once
// ============================================================================
//  Modbus 帧编解码
//  ---------------------------------------------------------------------------
//  PDU        : 功能码(1) + 数据(N)
//  RTU  帧    : 从站地址(1) + PDU + CRC16(2, 低字节在前)
//  MBAP 帧    : 事务标识(2) + 协议标识(2,=0) + 长度(2) + 单元标识(1) + PDU
//               长度字段 = 单元标识 + PDU 的字节数
// ============================================================================
#include "../common/ModbusDefs.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace mb {
namespace frame {

constexpr int kMbapSize = 7;   // MBAP 头长度
constexpr quint16 kProtocolId = 0x0000;

//! 构造 “读寄存器” 请求 PDU
QByteArray buildReadPdu(Func func, quint16 startAddr, quint16 count);

//! 构造 “写单个寄存器” 请求 PDU
QByteArray buildWriteSinglePdu(quint16 addr, quint16 value);

//! PDU -> RTU 帧（追加从站地址与 CRC）
QByteArray wrapRtu(quint8 unitId, const QByteArray& pdu);

//! PDU -> Modbus TCP 帧（追加 MBAP 头）
QByteArray wrapMbtcp(quint16 transactionId, quint8 unitId, const QByteArray& pdu);

//! 判断是否为异常响应（功能码最高位置 1），返回 true 并带出异常码
bool isExceptionPdu(const QByteArray& pdu, quint8* exceptionCode);

//! 解析 “读寄存器” 响应 PDU 为寄存器数组（大端）
bool parseReadPdu(const QByteArray& pdu, Func expectedFunc,
                  QVector<quint16>& out, QString* error);

//! 从接收缓冲区中切出一个完整 MBAP 帧；缓冲区不足时返回 false
bool takeMbtcpFrame(QByteArray& rxBuffer, QByteArray& frameOut);

//! 异常码 -> 中文描述
QString exceptionText(quint8 code);

//! 功能码（读类）-> 默认寄存器类型
Func readFuncFor(bool holding);

} // namespace frame
} // namespace mb
