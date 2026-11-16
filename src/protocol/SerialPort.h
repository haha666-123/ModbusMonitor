#pragma once
// ============================================================================
//  跨平台串口封装
//  ---------------------------------------------------------------------------
//  Windows : CreateFile + DCB + COMMTIMEOUTS + ReadFile/WriteFile
//  POSIX   : open + termios + read/write
//  之所以不用 QSerialPort：当前 Qt 安装未包含 SerialPort 模块，
//  这里直接对接操作系统串口 API，顺带兼容 Linux。
// ============================================================================
#include <QByteArray>
#include <QString>

namespace mb {

class SerialPort {
public:
    SerialPort();
    ~SerialPort();

    SerialPort(const SerialPort&)            = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool open(const QString& portName, int baudRate, QString* error);
    void close();
    [[nodiscard]] bool isOpen() const;

    //! 写入全部字节，返回实际写入数（-1 表示失败）
    qint64 write(const QByteArray& data);

    //! 在 timeoutMs 内读取最多 maxBytes 字节；无数据返回空
    QByteArray read(int maxBytes, int timeoutMs);

    //! 丢弃接收缓冲区中的残留数据
    void flushInput();

    [[nodiscard]] QString lastError() const { return m_lastError; }

private:
    void setError(const QString& msg);

#ifdef Q_OS_WIN
    void* m_handle = nullptr;      // HANDLE
#else
    int m_fd = -1;
#endif
    QString m_lastError;
};

} // namespace mb
