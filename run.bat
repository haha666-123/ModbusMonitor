@echo off
setlocal
REM ===========================================================================
REM  ModbusMonitor - one click build script (Windows / MinGW / Qt6 / Ninja)
REM
REM  Usage:
REM      run.bat            incremental build (Release)
REM      run.bat clean      wipe build dir and rebuild
REM      run.bat deploy     build + windeployqt (standalone folder)
REM
REM  Toolchain:
REM      Qt      6.8.1 mingw_64
REM      MinGW   g++ 13.1.0
REM      CMake   3.29.3
REM      Ninja   1.12.0
REM ===========================================================================

set "QT_ROOT=E:\MySofts\Qt"
set "QT_VER=6.8.1"
set "QT_DIR=%QT_ROOT%\%QT_VER%\mingw_64"
set "MINGW_DIR=%QT_ROOT%\Tools\mingw1310_64"
set "CMAKE_EXE=%QT_ROOT%\Tools\CMake_64\bin\cmake.exe"
set "NINJA_EXE=%QT_ROOT%\Tools\Ninja\ninja.exe"

set "PROJ_DIR=%~dp0"
set "BUILD_DIR=%PROJ_DIR%build"

echo ============================================================
echo   ModbusMonitor build script
echo ------------------------------------------------------------
echo   Project : %PROJ_DIR%
echo   Qt      : %QT_DIR%
echo   MinGW   : %MINGW_DIR%
echo ============================================================
echo.

REM ---------------------------------------------------------------- sanity check
if not exist "%CMAKE_EXE%" goto :no_cmake
if not exist "%NINJA_EXE%" goto :no_ninja
if not exist "%MINGW_DIR%\bin\g++.exe" goto :no_gcc
if not exist "%QT_DIR%\bin\qmake.exe" goto :no_qt

set "PATH=%MINGW_DIR%\bin;%QT_DIR%\bin;%PATH%"
set "CMAKE_PREFIX_PATH=%QT_DIR%"

REM ---------------------------------------------------------------- clean
if /I "%~1"=="clean" (
    echo [1/4] Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
) else (
    echo [1/4] Build directory: %BUILD_DIR%
)
echo.

REM ---------------------------------------------------------------- configure
echo [2/4] Configuring with CMake + Ninja...
"%CMAKE_EXE%" -S "%PROJ_DIR%." -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%" -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" "-DCMAKE_CXX_COMPILER=%MINGW_DIR%/bin/g++.exe"
if errorlevel 1 goto :cfg_fail
echo.

REM ---------------------------------------------------------------- build
echo [3/4] Building...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --parallel
if errorlevel 1 goto :build_fail
echo.

REM ---------------------------------------------------------------- deploy
if /I "%~1"=="deploy" (
    echo [4/4] Deploying Qt and MinGW runtime via deploy.bat...
    call "%PROJ_DIR%deploy.bat" "%QT_DIR%"
) else (
    echo [4/4] Deployment skipped. Use "run.bat deploy" to make a standalone folder.
)

echo.
echo ============================================================
echo   BUILD SUCCEEDED
echo ------------------------------------------------------------
echo   HMI       : %BUILD_DIR%\bin\ModbusMonitor.exe
echo   Simulator : %BUILD_DIR%\bin\ModbusSim.exe
echo   SmokeTest : %BUILD_DIR%\bin\mb_smoke.exe
echo ------------------------------------------------------------
echo   Quick start:
echo     1. start ModbusSim.exe      (Modbus TCP slave on 127.0.0.1:5020)
echo     2. start ModbusMonitor.exe  (HMI, click "start polling")
echo     3. mb_smoke.exe 127.0.0.1 5020 1 --full   (self test)
echo ============================================================
endlocal
exit /b 0

:no_cmake
echo [ERROR] CMake not found: %CMAKE_EXE%
goto :fail
:no_ninja
echo [ERROR] Ninja not found: %NINJA_EXE%
goto :fail
:no_gcc
echo [ERROR] g++ not found: %MINGW_DIR%\bin\g++.exe
goto :fail
:no_qt
echo [ERROR] Qt not found: %QT_DIR%
goto :fail
:cfg_fail
echo.
echo [ERROR] CMake configure failed.
goto :fail
:build_fail
echo.
echo [ERROR] Build failed.
goto :fail
:fail
echo ============================================================
echo   BUILD FAILED - see messages above
echo ============================================================
endlocal
exit /b 1
