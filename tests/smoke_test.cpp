// ============================================================================
//  冒烟测试：验证 Modbus 协议栈与采集链路
//  ---------------------------------------------------------------------------
//  用法（先启动 ModbusSim.exe）：
//      mb_smoke [host] [port] [unit]
//  默认：127.0.0.1 5020 1
//
//  验证内容：
//    1) MBAP 帧拼装与切帧
//    2) 功能码 0x03 请求与响应解析（大端）
//    3) Collector 采集线程 + 环形缓冲 + 有界队列
// ============================================================================
#include "../src/core/AiDiagnoser.h"
#include "../src/core/Collector.h"
#include "../src/core/Storage.h"
#include "../src/common/Console.h"
#include "../src/common/Crc16.h"
#include "../src/protocol/ModbusFrame.h"
#include "../src/protocol/ModbusTcpClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <QUrl>
#include <QVariant>

#include <chrono>
#include <cstdio>
#include <thread>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    mb::cprintf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++g_failures;
}

} // namespace

int main(int argc, char* argv[])
{
    mb::initConsoleUtf8();          // 控制台中文乱码修复

    QCoreApplication app(argc, argv);

    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 5020;
    quint8  unit = 1;
    if (argc > 1) host = QString::fromLocal8Bit(argv[1]);
    if (argc > 2) port = static_cast<quint16>(QString::fromLocal8Bit(argv[2]).toUShort());
    if (argc > 3) unit = static_cast<quint8>(QString::fromLocal8Bit(argv[3]).toUShort());

    {
        if (mb::consoleIsDirect()) {
            mb::cprintf("[编码] 直连控制台 -> WriteConsoleW(UTF-16)，不受代码页 %u 影响\n\n",
                        mb::consoleOutputCp());
        } else {
            mb::cprintf("[编码] 输出被重定向 -> 写入 UTF-8 字节（请按 UTF-8 打开文件）\n\n");
        }
    }
    mb::cprintf("=== Modbus 协议栈冒烟测试 -> %s:%u (unit %u)\n",
                qUtf8Printable(host), static_cast<unsigned>(port), static_cast<unsigned>(unit));

    // ---------------------------------------------------------------- 1) CRC
    mb::cprintf("\n[1] CRC16 校验\n");
    {
        // 标准样例：01 03 00 00 00 01 -> CRC 0x84 0x0A
        const QByteArray body = QByteArray::fromHex("010300000001");
        const QByteArray withCrc = mb::rtuAppendCrc(body);
        check(withCrc.toHex() == "010300000001840a",
              "RTU 已知向量 CRC 计算正确 (期望 ...840a)");
        check(mb::rtuCheckCrc(withCrc), "CRC 自校验通过");
        QByteArray bad = withCrc;
        bad[bad.size() - 1] = static_cast<char>(bad.at(bad.size() - 1) ^ 0xFF);
        check(!mb::rtuCheckCrc(bad), "篡改后 CRC 校验失败（负向用例）");
    }

    // ---------------------------------------------------------------- 2) 帧编解码
    mb::cprintf("\n[2] 帧编解码\n");
    {
        const QByteArray pdu = mb::frame::buildReadPdu(mb::Func::ReadHoldingRegs, 0, 2);
        check(pdu.toHex() == "0300000002", "读请求 PDU 编码正确 (0300000002)");

        const QByteArray mbap = mb::frame::wrapMbtcp(0x1234, 1, pdu);
        check(mbap.size() == 7 + pdu.size(), "MBAP 帧长度 = 7 + PDU");
        check(mbap.left(2).toHex() == "1234", "事务标识写入正确");
        check(mbap.mid(4, 2).toHex() == "0006", "长度字段 = 单元标识 + PDU(=6)");

        // 模拟一次正常响应：04 温度, 0x00FA(=250)
        QByteArray respPdu = QByteArray::fromHex("030400fa0064");
        QVector<quint16> regs;
        QString err;
        const bool ok = mb::frame::parseReadPdu(respPdu, mb::Func::ReadHoldingRegs, regs, &err);
        check(ok && regs.size() == 2 && regs[0] == 250 && regs[1] == 100,
              "读响应解析为大端寄存器数组 (250, 100)");

        // 异常响应
        QByteArray exPdu = QByteArray::fromHex("8302");
        check(!mb::frame::parseReadPdu(exPdu, mb::Func::ReadHoldingRegs, regs, &err),
              "异常响应被识别并返回失败");
    }

    // ---------------------------------------------------------------- 3) 真实链路
    mb::cprintf("\n[3] 通过 TCP 实际读取模拟器寄存器\n");
    {
        mb::ModbusTcpClient client(host, port, 1000);
        QString err;
        if (!client.open(&err)) {
            mb::cprintf("  [SKIP] 无法连接模拟器: %s\n", qUtf8Printable(err));
            mb::cprintf("         请先运行 ModbusSim.exe\n");
            ++g_failures;
        } else {
            QVector<quint16> regs;
            const bool ok = client.readRegisters(unit, mb::Func::ReadHoldingRegs,
                                                 0, 6, regs, &err);
            check(ok, "功能码 0x03 读取 6 个寄存器成功");
            if (ok) {
                mb::cprintf("         原始值: ");
                for (int i = 0; i < regs.size(); ++i)
                    mb::cprintf("%u ", static_cast<unsigned>(regs.at(i)));
                mb::cprintf("\n");
                check(regs.size() == 6, "返回寄存器数量正确");
                check(regs[0] > 0, "温度寄存器非零");
            } else {
                mb::cprintf("         错误: %s\n", qUtf8Printable(err));
            }

            // 越界地址应触发异常码 0x02
            QVector<quint16> bad;
            const bool badOk = client.readRegisters(unit, mb::Func::ReadHoldingRegs,
                                                    9000, 4, bad, &err);
            check(!badOk, "越界地址被从站拒绝（异常响应）");
            client.close();
        }
    }

    // ---------------------------------------------------------------- 4) 采集线程
    mb::cprintf("\n[4] Collector 采集线程（1.5 秒）\n");
    {
        mb::Collector collector;
        mb::CollectorConfig cfg;
        cfg.mode = mb::LinkMode::Tcp;
        cfg.host = host;
        cfg.port = port;
        cfg.unitId = unit;
        cfg.pollIntervalMs = 200;
        cfg.timeoutMs = 800;
        collector.setConfig(cfg);
        collector.setRegisters({
            {0, QStringLiteral("温度"), QStringLiteral("℃"), 0.1, 20.0, 80.0, true},
            {1, QStringLiteral("压力"), QStringLiteral("kPa"), 0.1, 0.0, 100.0, true},
        });

        QString err;
        if (!collector.start(&err)) {
            mb::cprintf("  [SKIP] Collector 启动失败: %s\n", qUtf8Printable(err));
            ++g_failures;
        } else {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 1500)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

            const mb::CollectorStats st = collector.stats();
            collector.stop();

            mb::cprintf("         轮询 %llu 次, 成功 %llu, 失败 %llu, 采样点 %llu\n",
                        static_cast<unsigned long long>(st.polls),
                        static_cast<unsigned long long>(st.succeeded),
                        static_cast<unsigned long long>(st.failed),
                        static_cast<unsigned long long>(st.points));

            check(st.polls >= 4, "采集线程按 200ms 周期持续轮询");
            check(st.points > 0, "产生了采样点");
            check(collector.storeQueue().size() > 0, "数据进入存储队列");
            check(!collector.history(0, 10).empty(), "环形缓冲里有历史数据");
        }
    }

    // ---------------------------------------------------------------- 5) 存储链路
    const bool fullMode = (argc > 4 && QString::fromLocal8Bit(argv[4]) == QStringLiteral("--full"));
    mb::cprintf("\n[5] 存储链路（Storage 批量事务写入）%s\n",
                fullMode ? "（--full：跑 25 秒以捕捉告警）" : "（快速：4 秒）");
    {
        const QString dbPath = QDir(QDir::tempPath()).filePath(
            QStringLiteral("mb_smoke_test.db"));
        QFile::remove(dbPath);
        QFile::remove(dbPath + QStringLiteral("-wal"));
        QFile::remove(dbPath + QStringLiteral("-shm"));

        mb::Storage storage;
        QString err;
        const bool opened = storage.open(dbPath, &err);
        check(opened, "打开 SQLite 数据库文件");
        if (!opened) {
            mb::cprintf("         错误: %s\n", qUtf8Printable(err));
        } else {
            mb::Collector c2;
            mb::CollectorConfig cfg2;
            cfg2.mode = mb::LinkMode::Tcp;
            cfg2.host = host;
            cfg2.port = port;
            cfg2.unitId = unit;
            cfg2.pollIntervalMs = 200;
            cfg2.timeoutMs = 800;
            c2.setConfig(cfg2);
            c2.setRegisters({
                {0, QStringLiteral("温度"), QStringLiteral("℃"), 0.1, 20.0, 80.0, true},
                {1, QStringLiteral("压力"), QStringLiteral("kPa"), 0.1, 0.0, 100.0, true},
            });

            storage.setSource(&c2.storeQueue());
            storage.start();

            QString startErr;
            if (!c2.start(&startErr)) {
                mb::cprintf("         Collector 启动失败: %s\n", qUtf8Printable(startErr));
                ++g_failures;
            } else {
                const int seconds = fullMode ? 25 : 4;
                QElapsedTimer t;
                t.start();
                int shown = 0;
                while (t.elapsed() < seconds * 1000) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    // 实时把告警打出来，并交给存储层（与界面层做法一致）
                    for (const mb::Alarm& a : c2.takeNewAlarms()) {
                        ++shown;
                        storage.saveAlarm(a);
                        mb::cprintf("         告警: addr=%u value=%.2f (limit %.2f, %s)\n",
                                    static_cast<unsigned>(a.addr), a.value, a.limit,
                                    a.isHigh ? "high" : "low");
                    }
                }
                c2.stop();
                mb::cprintf("         采集 %d 秒，产生告警 %d 条\n", seconds, shown);
                if (fullMode)
                    check(shown > 0, "模拟器越限被识别为告警");
            }

            // 让存储线程把队列排空
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            storage.stop();
            storage.close();

            check(storage.inserted() > 0, "采样点已批量写入数据库");

            // 复核数据库内容
            {
                const QString conn = QStringLiteral("verify_conn");
                {
                    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
                    db.setDatabaseName(dbPath);
                    if (db.open()) {
                        QSqlQuery q(db);
                        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM datapoint")) && q.next())
                            mb::cprintf("         数据库 datapoint 行数: %d\n", q.value(0).toInt());
                        if (q.exec(QStringLiteral("SELECT COUNT(*) FROM alarm")) && q.next())
                            mb::cprintf("         数据库 alarm 行数: %d\n", q.value(0).toInt());
                        db.close();
                    }
                }
                QSqlDatabase::removeDatabase(conn);
            }
        }
    }

    // ---------------------------------------------------------------- 6) AI 地址规范化
    std::printf("\n[6] AI 接口地址规范化\n");
    {
        auto norm = [](const char* s) {
            return mb::AiDiagnoser::normalizeUrl(QUrl(QString::fromUtf8(s))).toString();
        };
        auto prob = [](const char* s) {
            return mb::AiDiagnoser::endpointProblem(QUrl(QString::fromUtf8(s)));
        };

        check(norm("https://api.deepseek.com") == "https://api.deepseek.com/chat/completions",
              "裸域名自动补全为 /chat/completions");
        check(norm("https://api.deepseek.com/v1") == "https://api.deepseek.com/v1/chat/completions",
              "/v1 结尾自动补全为 /v1/chat/completions");
        check(norm("https://api.openai.com/v1/chat/completions")
                  == "https://api.openai.com/v1/chat/completions",
              "完整端点保持原样");
        check(!prob("https://platform.deepseek.com/api_keys").isEmpty(),
              "识别出网页控制台地址并给出提示");
        check(prob("https://platform.deepseek.com/api_keys").contains("chat/completions"),
              "提示中包含正确地址示例");
        check(prob("https://api.deepseek.com/chat/completions").isEmpty(),
              "合法端点不产生告警");
    }

    // ---------------------------------------------------------------- 汇总
    mb::cprintf("\n=== 结果: %s (失败项 %d)\n",
                g_failures == 0 ? "全部通过" : "存在失败", g_failures);
    mb::restoreConsole();
    return g_failures == 0 ? 0 : 1;
}
