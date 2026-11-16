cmake_minimum_required(VERSION 3.21)

project(ModbusMonitor
    VERSION 1.0.0
    DESCRIPTION "Qt based Modbus data acquisition and monitoring platform"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ------------------------------------------------------------------ Qt
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Sql Charts)

# ------------------------------------------------------------------ core library
set(MB_CORE_SRC
    src/common/Console.cpp
    src/protocol/ModbusFrame.cpp
    src/protocol/SerialPort.cpp
    src/protocol/ModbusTcpClient.cpp
    src/protocol/ModbusRtuClient.cpp
    src/core/Collector.cpp
    src/core/Storage.cpp
    src/core/AiDiagnoser.cpp
)

set(MB_CORE_HDR
    src/common/Console.h
    src/common/ModbusDefs.h
    src/common/Crc16.h
    src/common/Containers.h
    src/protocol/IModbusClient.h
    src/protocol/ModbusFrame.h
    src/protocol/SerialPort.h
    src/protocol/ModbusTcpClient.h
    src/protocol/ModbusRtuClient.h
    src/core/Collector.h
    src/core/Storage.h
    src/core/AiDiagnoser.h
)

add_library(mbcore STATIC ${MB_CORE_SRC} ${MB_CORE_HDR})

target_include_directories(mbcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(mbcore PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)

if(WIN32)
    target_link_libraries(mbcore PUBLIC kernel32)
endif()

# ------------------------------------------------------------------ main application
set(APP_SRC
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/RealtimeTableModel.cpp
    src/ui/RealtimeTableModel.h
    src/ui/TrendChart.cpp
    src/ui/TrendChart.h
)

add_executable(ModbusMonitor ${APP_SRC})

target_link_libraries(ModbusMonitor PRIVATE
    mbcore
    Qt6::Widgets
    Qt6::Charts
)

# Console window is kept for demonstration / log output.
# Uncomment the line below to build a windowed (no console) binary.
# set_target_properties(ModbusMonitor PROPERTIES WIN32_EXECUTABLE TRUE)

# ------------------------------------------------------------------ slave simulator
add_executable(ModbusSim sim/ModbusSim.cpp)
target_link_libraries(ModbusSim PRIVATE mbcore Qt6::Network)

# ------------------------------------------------------------------ smoke test
add_executable(mb_smoke tests/smoke_test.cpp)
target_link_libraries(mb_smoke PRIVATE mbcore Qt6::Network)

# ------------------------------------------------------------------ console encoding check
add_executable(mb_console_check tests/console_check.cpp)
target_link_libraries(mb_console_check PRIVATE mbcore)

# ------------------------------------------------------------------ output layout
set_target_properties(ModbusMonitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set_target_properties(ModbusSim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set_target_properties(mb_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set_target_properties(mb_console_check PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ------------------------------------------------------------------ deploy helper
# 每次构建后把 deploy.bat 复制到可执行文件目录，方便直接双击运行
add_custom_command(TARGET ModbusMonitor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/deploy.bat"
            "${CMAKE_BINARY_DIR}/bin/deploy.bat")

message(STATUS "Qt version      : ${Qt6_VERSION}")
message(STATUS "Compiler        : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build type      : ${CMAKE_BUILD_TYPE}")
