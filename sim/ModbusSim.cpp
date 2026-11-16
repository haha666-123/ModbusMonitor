// ============================================================================
//  Modbus TCP 从站模拟器
//  ---------------------------------------------------------------------------
//  没有真实 PLC 时，用它扮演一台设备：监听 TCP 端口，响应功能码 0x03/0x04
//  的读寄存器请求，数值带正弦变化与噪声，并周期性产生一次越限，
//  方便演示 “采集 -> 告警 -> AI 诊断” 的完整链路。
//
//  用法： ModbusSim.exe [端口] [从站地址]
//  默认： 5020 端口，从站地址 1
// ============================================================================
#include "../src/common/Console.h"
#include "../src/protocol/ModbusFrame.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cmath>
#include <cstdio>

namespace {

constexpr int kRegisterCount = 6;

class SlaveSim {
public:
    explicit SlaveSim(quint16 port, quint8 unitId)
        : m_unitId(unitId)
    {
        m_regs.fill(0, kRegisterCount);

        QObject::connect(&m_server, &QTcpServer::newConnection, [this] {
            while (QTcpSocket* sock = m_server.nextPendingConnection()) {
                m_buffers.insert(sock, QByteArray());
                mb::cprintf("[sim] client connected: %s\n",
                            qUtf8Printable(sock->peerAddress().toString()));

                QObject::connect(sock, &QTcpSocket::readyRead, [this, sock] {
                    onReadyRead(sock);
                });
                QObject::connect(sock, &QTcpSocket::disconnected, [this, sock] {
                    m_buffers.remove(sock);
                    sock->deleteLater();
                    mb::cprintf("[sim] client disconnected\n");
                });
            }
        });

        QObject::connect(&m_timer, &QTimer::timeout, [this] { tick(); });
        m_timer.setInterval(500);

        m_server.listen(QHostAddress::Any, port);
        if (m_server.isListening()) {
            mb::cprintf("[sim] Modbus TCP slave listening on 0.0.0.0:%u (unit %u)\n",
                        static_cast<unsigned>(port), static_cast<unsigned>(unitId));
            mb::cprintf("[sim] registers: 0=温度 1=压力 2=液位 3=转速 4=电流 5=电压\n");
            m_timer.start();
        } else {
            mb::cprintf("[sim] listen failed: %s\n", qUtf8Printable(m_server.errorString()));
        }
    }

    [[nodiscard]] bool listening() const { return m_server.isListening(); }

private:
    // ------------------------------------------------------------- 数据生成
    void tick()
    {
        ++m_tick;
        const double t = static_cast<double>(m_tick);

        auto noise = [](int amplitude) {
            return (std::rand() % (2 * amplitude + 1)) - amplitude;
        };

        // 温度: 25℃ 附近波动；每 40 个周期(20 秒)左右冲高一次，触发上限告警
        double temp = 250.0 + 30.0 * std::sin(t / 12.0) + noise(4);
        if (m_tick % 40 >= 34)
            temp = 850.0 + noise(10);          // 85℃，超过 80℃ 上限
        m_regs[0] = clamp(temp);

        m_regs[1] = clamp(500.0 + 60.0 * std::sin(t / 7.0) + noise(8));    // 压力
        m_regs[2] = clamp(500.0 + 180.0 * std::sin(t / 21.0) + noise(6));  // 液位
        m_regs[3] = clamp(1500.0 + 120.0 * std::sin(t / 5.0) + noise(20)); // 转速
        m_regs[4] = clamp(500.0 + 70.0 * std::sin(t / 6.0) + noise(10));   // 电流
        m_regs[5] = clamp(2200.0 + 40.0 * std::sin(t / 9.0) + noise(12));  // 电压
    }

    static quint16 clamp(double v)
    {
        if (v < 0.0) v = 0.0;
        if (v > 65535.0) v = 65535.0;
        return static_cast<quint16>(v);
    }

    // ------------------------------------------------------------- 协议处理
    void onReadyRead(QTcpSocket* sock)
    {
        QByteArray& buf = m_buffers[sock];
        buf.append(sock->readAll());

        QByteArray frame;
        while (mb::frame::takeMbtcpFrame(buf, frame))
            handleFrame(sock, frame);
    }

    void handleFrame(QTcpSocket* sock, const QByteArray& frame)
    {
        if (frame.size() < mb::frame::kMbapSize + 1)
            return;

        const quint16 tid =
            static_cast<quint16>((static_cast<quint8>(frame.at(0)) << 8) |
                                 static_cast<quint8>(frame.at(1)));
        const quint8 unit = static_cast<quint8>(frame.at(6));
        const QByteArray pdu = frame.mid(mb::frame::kMbapSize);

        if (unit != m_unitId && unit != 0) {
            sendException(sock, tid, unit, static_cast<quint8>(pdu.at(0)),
                          static_cast<quint8>(mb::Exception::SlaveDeviceFailure));
            return;
        }

        const quint8 func = static_cast<quint8>(pdu.at(0));

        if (func != static_cast<quint8>(mb::Func::ReadHoldingRegs) &&
            func != static_cast<quint8>(mb::Func::ReadInputRegs)) {
            sendException(sock, tid, unit, func,
                          static_cast<quint8>(mb::Exception::IllegalFunction));
            return;
        }

        if (pdu.size() < 5) {
            sendException(sock, tid, unit, func,
                          static_cast<quint8>(mb::Exception::IllegalDataValue));
            return;
        }

        const quint16 start =
            static_cast<quint16>((static_cast<quint8>(pdu.at(1)) << 8) |
                                 static_cast<quint8>(pdu.at(2)));
        const quint16 count =
            static_cast<quint16>((static_cast<quint8>(pdu.at(3)) << 8) |
                                 static_cast<quint8>(pdu.at(4)));

        if (count == 0 || count > mb::kMaxReadRegs ||
            start + count > static_cast<quint16>(m_regs.size())) {
            sendException(sock, tid, unit, func,
                          static_cast<quint8>(mb::Exception::IllegalDataAddress));
            return;
        }

        QByteArray respPdu;
        respPdu.append(static_cast<char>(func));
        respPdu.append(static_cast<char>(static_cast<quint8>(count * 2)));
        for (quint16 i = 0; i < count; ++i) {
            const quint16 v = m_regs.at(static_cast<int>(start + i));
            respPdu.append(static_cast<char>((v >> 8) & 0xFF));
            respPdu.append(static_cast<char>(v & 0xFF));
        }

        sock->write(mb::frame::wrapMbtcp(tid, unit, respPdu));
    }

    void sendException(QTcpSocket* sock, quint16 tid, quint8 unit,
                       quint8 func, quint8 code)
    {
        QByteArray pdu;
        pdu.append(static_cast<char>(func | 0x80));
        pdu.append(static_cast<char>(code));
        sock->write(mb::frame::wrapMbtcp(tid, unit, pdu));
    }

    QTcpServer m_server;
    QTimer     m_timer;
    QVector<quint16> m_regs;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    quint8 m_unitId = 1;
    int    m_tick   = 0;
};

} // namespace

int main(int argc, char* argv[])
{
    mb::initConsoleUtf8();          // 控制台中文乱码修复

    QCoreApplication app(argc, argv);

    quint16 port = 5020;
    quint8  unit = 1;

    if (argc > 1)
        port = static_cast<quint16>(QString::fromLocal8Bit(argv[1]).toUShort());
    if (argc > 2)
        unit = static_cast<quint8>(QString::fromLocal8Bit(argv[2]).toUShort());

    SlaveSim sim(port, unit);
    if (!sim.listening()) {
        mb::restoreConsole();
        return 1;
    }

    const int rc = app.exec();
    mb::restoreConsole();
    return rc;
}
