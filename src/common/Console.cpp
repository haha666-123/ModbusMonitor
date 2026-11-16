#include "Console.h"

#include <cstdarg>
#include <cstdio>
#include <string>

#ifdef _WIN32   // 用编译器内置宏，不依赖 Qt 头文件
#  include <windows.h>
#endif

namespace mb {

#ifdef _WIN32   // 用编译器内置宏，不依赖 Qt 头文件
namespace {
UINT   g_prevOut     = 0;
UINT   g_prevIn      = 0;
bool   g_saved       = false;
bool   g_probed      = false;
bool   g_hasConsole  = false;
HANDLE g_consoleOut  = nullptr;
} // namespace
#endif

void initConsoleUtf8()
{
    // 不缓冲：重定向时逐次落盘，进程被强制结束也不丢日志
    std::setvbuf(stdout, nullptr, _IONBF, 0);

#ifdef _WIN32   // 用编译器内置宏，不依赖 Qt 头文件
    if (!g_saved) {
        g_prevOut = GetConsoleOutputCP();
        g_prevIn  = GetConsoleCP();
        g_saved   = true;
    }

    // 仍然切 UTF-8：让走窄字节的第三方输出（如 Qt 内部日志）也按 UTF-8 解释
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // 探测 stdout 是否直连控制台；这决定 cprintf 走哪条输出路径
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  mode = 0;
    g_hasConsole = (h != nullptr && h != INVALID_HANDLE_VALUE &&
                    GetConsoleMode(h, &mode) != 0);
    g_consoleOut = h;
    g_probed = true;
#endif
}

void restoreConsole()
{
#ifdef _WIN32   // 用编译器内置宏，不依赖 Qt 头文件
    if (!g_saved)
        return;
    if (g_prevOut)
        SetConsoleOutputCP(g_prevOut);
    if (g_prevIn)
        SetConsoleCP(g_prevIn);
#endif
}

unsigned consoleOutputCp()
{
#ifdef _WIN32   // 用编译器内置宏，不依赖 Qt 头文件
    return static_cast<unsigned>(GetConsoleOutputCP());
#else
    return 0;
#endif
}

bool consoleIsDirect()
{
#ifdef _WIN32   // 用编译器内置宏，不依赖 Qt 头文件
    if (!g_probed)
        initConsoleUtf8();
    return g_hasConsole;
#else
    return true;   // POSIX 终端本身按 UTF-8 处理，直接写即可
#endif
}

int cprintf(const char* utf8Format, ...)
{
    char    stackBuf[2048];
    va_list ap;

    va_start(ap, utf8Format);
    const int need = std::vsnprintf(stackBuf, sizeof(stackBuf), utf8Format, ap);
    va_end(ap);
    if (need < 0)
        return need;

    const char* text = stackBuf;
    int         len  = need;
    std::string heap;

    if (static_cast<size_t>(need) >= sizeof(stackBuf)) {
        heap.resize(static_cast<size_t>(need) + 1);
        va_start(ap, utf8Format);
        std::vsnprintf(&heap[0], heap.size(), utf8Format, ap);
        va_end(ap);
        text = heap.c_str();
        len  = need;
    }

#ifdef _WIN32   // 用编译器内置宏，不依赖 Qt 头文件
    if (!g_probed)
        initConsoleUtf8();

    if (g_hasConsole) {
        // UTF-8 -> UTF-16，交给控制台原生渲染，绕过代码页
        const int wlen = MultiByteToWideChar(CP_UTF8, 0, text, len, nullptr, 0);
        if (wlen > 0) {
            std::wstring w(static_cast<size_t>(wlen), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text, len, &w[0], wlen);
            DWORD written = 0;
            if (WriteConsoleW(g_consoleOut, w.c_str(),
                              static_cast<DWORD>(w.size()), &written, nullptr))
                return len;
        }
    }
#endif

    // 重定向 / 管道 / 非 Windows：直接写 UTF-8 字节
    return static_cast<int>(std::fwrite(text, 1, static_cast<size_t>(len), stdout));
}

} // namespace mb
