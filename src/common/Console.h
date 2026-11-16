#pragma once
// ============================================================================
//  控制台输出适配
//  ---------------------------------------------------------------------------
//  背景：Windows 控制台默认代码页 936(GBK)，而源码中文字面量编译后是 UTF-8 字节，
//        直接 printf 会显示乱码；且 SetConsoleOutputCP(CP_UTF8) 在部分终端
//        （Windows Terminal / ConPTY / 被重定向）里不一定生效。
//
//  方案：不再依赖代码页。mb::cprintf() 先格式化成 UTF-8，
//        * 若 stdout 是真控制台 → 转 UTF-16 用 WriteConsoleW 输出（控制台原生渲染）
//        * 若被重定向/管道   → 直接写 UTF-8 字节
//        两条路都不受当前代码页影响。
//
//  用法：把 std::printf 换成 mb::cprintf 即可，格式串写法完全一致。
// ============================================================================
// 注意：本模块不包含 Qt 头文件，平台判断统一使用编译器内置的 _WIN32，
// 不能用 Q_OS_WIN（该宏只在包含 <QtGlobal> 后才存在）。
namespace mb {

//! 初始化：关闭 stdout 缓冲、尽力把代码页切到 UTF-8、探测控制台句柄
void initConsoleUtf8();

//! 恢复 initConsoleUtf8() 之前的代码页
void restoreConsole();

//! 当前控制台输出代码页；无控制台返回 0
unsigned consoleOutputCp();

//! stdout 是否直连真实控制台（true=走 WriteConsoleW，false=重定向到文件/管道）
bool consoleIsDirect();

//! UTF-8 版 printf，自动适配控制台 / 重定向
int cprintf(const char* utf8Format, ...);

} // namespace mb
