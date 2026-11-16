// ============================================================================
//  控制台编码自检
//  ---------------------------------------------------------------------------
//  做法：确保进程有控制台 -> 用 mb::cprintf 写一行中文 ->
//        再用 ReadConsoleOutputCharacterW 从屏幕缓冲区读回来逐字符比对。
//
//  这样即使没人盯着屏幕，也能自动证明“写出的中文被控制台正确接收”，
//  从而验证走的是 WriteConsoleW(UTF-16) 而不是依赖代码页的窄字节输出。
//
//  用法： mb_console_check [结果文件路径]
//        结果写入文件（不写 stdout），所以运行本程序时不要重定向 stdout，
//        否则 stdout 不再直连控制台，就测不到目标路径了。
//
//  退出码：0=通过  1=内容不符  2=无法获得控制台  3/4=API 失败
// ============================================================================
#include "../src/common/Console.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace {

int writeResult(const char* path, const char* text)
{
    if (FILE* f = std::fopen(path, "wb")) {
        std::fwrite(text, 1, std::char_traits<char>::length(text), f);
        std::fclose(f);
        return 0;
    }
    return -1;
}

} // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    const char* resultPath = (argc > 1) ? argv[1] : "concheck_result.txt";

    // ---------------------------------------------------------------- 确保有控制台
    DWORD  mode = 0;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    if (!hOut || hOut == INVALID_HANDLE_VALUE || !GetConsoleMode(hOut, &mode)) {
        // stdout 不是控制台：尝试新建一个（若已有控制台会失败，属正常）
        if (!AllocConsole()) {
            const std::string msg =
                "[SKIP] stdout 未直连控制台，且无法新建控制台（错误码 " +
                std::to_string(GetLastError()) +
                "）。请勿重定向 stdout 后运行本程序。\n";
            writeResult(resultPath, msg.c_str());
            return 2;
        }
        hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    }

    mb::initConsoleUtf8();

    // ---------------------------------------------------------------- 写探针文本
    const std::string probe = "编码自检: 温度 压力 液位 转速 27.5";
    mb::cprintf("%s", probe.c_str());     // 不换行，便于按光标定位

    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(hOut, &info)) {
        writeResult(resultPath, "[FAIL] GetConsoleScreenBufferInfo 失败\n");
        return 3;
    }

    const COORD start{ 0, info.dwCursorPosition.Y };
    wchar_t     wbuf[512] = {};
    DWORD       read = 0;
    if (!ReadConsoleOutputCharacterW(hOut, wbuf, 400, start, &read)) {
        writeResult(resultPath, "[FAIL] ReadConsoleOutputCharacterW 失败\n");
        return 4;
    }

    // ---------------------------------------------------------------- 读回比对
    char ubuf[1024] = {};
    WideCharToMultiByte(CP_UTF8, 0, wbuf, static_cast<int>(read),
                        ubuf, sizeof(ubuf) - 1, nullptr, nullptr);

    std::string got(ubuf);
    while (!got.empty() && (got.back() == ' ' || got.back() == '\0'))
        got.pop_back();

    const bool same = (got == probe);

    std::string out;
    out += same ? "[PASS] 控制台读回校验通过\n" : "[FAIL] 控制台读回内容不符\n";
    out += "  输出方式    : ";
    out += mb::consoleIsDirect() ? "WriteConsoleW(UTF-16)" : "fwrite(UTF-8)";
    out += "\n  控制台代码页: " + std::to_string(mb::consoleOutputCp()) + "\n";
    out += "  期望(UTF-8) : " + probe + "\n";
    out += "  实际(UTF-8) : " + got + "\n";

    writeResult(resultPath, out.c_str());
    return same ? 0 : 1;
#else
    const char* resultPath = (argc > 1) ? argv[1] : "concheck_result.txt";
    writeResult(resultPath, "[SKIP] 非 Windows 平台\n");
    return 0;
#endif
}
