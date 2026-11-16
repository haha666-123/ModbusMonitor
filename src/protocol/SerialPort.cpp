#include "SerialPort.h"

#include <QElapsedTimer>
#include <QThread>

#ifdef Q_OS_WIN
#  include <windows.h>
#else
#  include <cerrno>
#  include <cstring>
#  include <fcntl.h>
#  include <termios.h>
#  include <unistd.h>
#endif

namespace mb {

SerialPort::SerialPort() = default;

SerialPort::~SerialPort()
{
    close();
}

void SerialPort::setError(const QString& msg)
{
    m_lastError = msg;
}

bool SerialPort::isOpen() const
{
#ifdef Q_OS_WIN
    return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
#else
    return m_fd >= 0;
#endif
}

// ===========================================================================
//  Windows 实现
// ===========================================================================
#ifdef Q_OS_WIN

bool SerialPort::open(const QString& portName, int baudRate, QString* error)
{
    close();

    // COM10 及以上必须使用 \\.\ 前缀
    const QString device = QStringLiteral("\\\\.\\") + portName;
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(device.utf16()),
                           GENERIC_READ | GENERIC_WRITE,
                           0,        // 独占访问
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        setError(QStringLiteral("打开串口失败 %1 (错误码 %2)").arg(portName).arg(GetLastError()));
        if (error) *error = m_lastError;
        return false;
    }

    SetupComm(h, 64 * 1024, 64 * 1024);

    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(h, &dcb)) {
        setError(QStringLiteral("GetCommState 失败"));
        CloseHandle(h);
        if (error) *error = m_lastError;
        return false;
    }

    dcb.BaudRate = static_cast<DWORD>(baudRate);
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    dcb.fOutxCtsFlow    = FALSE;
    dcb.fOutxDsrFlow    = FALSE;
    dcb.fDtrControl     = DTR_CONTROL_ENABLE;
    dcb.fRtsControl     = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX  = FALSE;

    if (!SetCommState(h, &dcb)) {
        setError(QStringLiteral("SetCommState 失败（波特率 %1 不受支持？）").arg(baudRate));
        CloseHandle(h);
        if (error) *error = m_lastError;
        return false;
    }

    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout         = 20;    // 字节间最大间隔
    to.ReadTotalTimeoutMultiplier  = 0;
    to.ReadTotalTimeoutConstant    = 120;   // 单次 ReadFile 最长等待
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 500;
    SetCommTimeouts(h, &to);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);

    m_handle = h;
    return true;
}

void SerialPort::close()
{
    if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
}

qint64 SerialPort::write(const QByteArray& data)
{
    if (!isOpen())
        return -1;

    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(m_handle), data.constData(),
                   static_cast<DWORD>(data.size()), &written, nullptr)) {
        setError(QStringLiteral("WriteFile 失败 (错误码 %1)").arg(GetLastError()));
        return -1;
    }
    return static_cast<qint64>(written);
}

QByteArray SerialPort::read(int maxBytes, int timeoutMs)
{
    QByteArray out;
    if (!isOpen() || maxBytes <= 0)
        return out;

    QElapsedTimer timer;
    timer.start();

    while (out.size() < maxBytes && timer.elapsed() < timeoutMs) {
        char buf[512];
        const int want = qMin<int>(static_cast<int>(sizeof(buf)), maxBytes - out.size());
        DWORD got = 0;
        if (!ReadFile(static_cast<HANDLE>(m_handle), buf, static_cast<DWORD>(want), &got, nullptr)) {
            setError(QStringLiteral("ReadFile 失败 (错误码 %1)").arg(GetLastError()));
            break;
        }
        if (got > 0) {
            out.append(buf, static_cast<int>(got));
        } else {
            // 没有数据：短暂让出 CPU，避免忙等
            QThread::msleep(2);
        }
    }
    return out;
}

void SerialPort::flushInput()
{
    if (isOpen())
        PurgeComm(static_cast<HANDLE>(m_handle), PURGE_RXCLEAR);
}

// ===========================================================================
//  POSIX 实现
// ===========================================================================
#else

namespace {
speed_t toSpeed(int baud, bool* ok)
{
    *ok = true;
    switch (baud) {
    case 1200:   return B1200;
    case 2400:   return B2400;
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default:     *ok = false; return B9600;
    }
}
} // namespace

bool SerialPort::open(const QString& portName, int baudRate, QString* error)
{
    close();

    const int fd = ::open(portName.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        setError(QStringLiteral("打开串口失败 %1: %2").arg(portName, QString::fromLocal8Bit(strerror(errno))));
        if (error) *error = m_lastError;
        return false;
    }

    termios tio{};
    if (tcgetattr(fd, &tio) != 0) {
        setError(QStringLiteral("tcgetattr 失败"));
        ::close(fd);
        if (error) *error = m_lastError;
        return false;
    }

    bool speedOk = false;
    const speed_t sp = toSpeed(baudRate, &speedOk);
    if (!speedOk) {
        setError(QStringLiteral("不支持的波特率 %1").arg(baudRate));
        ::close(fd);
        if (error) *error = m_lastError;
        return false;
    }

    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);

    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~PARENB;      // 无校验
    tio.c_cflag &= ~CSTOPB;      // 1 位停止位
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;          // 8 位数据位
    tio.c_cflag &= ~CRTSCTS;     // 无硬件流控

    tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);
    tio.c_iflag &= ~(INLCR | ICRNL | IGNCR);
    tio.c_oflag &= ~OPOST;

    tio.c_cc[VMIN]  = 0;         // 非阻塞读
    tio.c_cc[VTIME] = 1;         // 100ms 粒度

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        setError(QStringLiteral("tcsetattr 失败"));
        ::close(fd);
        if (error) *error = m_lastError;
        return false;
    }

    tcflush(fd, TCIOFLUSH);
    m_fd = fd;
    return true;
}

void SerialPort::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

qint64 SerialPort::write(const QByteArray& data)
{
    if (!isOpen())
        return -1;

    qint64 total = 0;
    while (total < data.size()) {
        const ssize_t n = ::write(m_fd, data.constData() + total,
                                  static_cast<size_t>(data.size() - total));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                QThread::msleep(2);
                continue;
            }
            setError(QStringLiteral("write 失败: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            return -1;
        }
        total += n;
    }
    return total;
}

QByteArray SerialPort::read(int maxBytes, int timeoutMs)
{
    QByteArray out;
    if (!isOpen() || maxBytes <= 0)
        return out;

    QElapsedTimer timer;
    timer.start();

    while (out.size() < maxBytes && timer.elapsed() < timeoutMs) {
        char buf[512];
        const int want = qMin<int>(static_cast<int>(sizeof(buf)), maxBytes - out.size());
        const ssize_t n = ::read(m_fd, buf, static_cast<size_t>(want));
        if (n > 0) {
            out.append(buf, static_cast<int>(n));
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            setError(QStringLiteral("read 失败: %1").arg(QString::fromLocal8Bit(strerror(errno))));
            break;
        } else {
            QThread::msleep(2);
        }
    }
    return out;
}

void SerialPort::flushInput()
{
    if (isOpen())
        tcflush(m_fd, TCIFLUSH);
}

#endif

} // namespace mb
