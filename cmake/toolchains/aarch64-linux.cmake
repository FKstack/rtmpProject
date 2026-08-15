# Linux ARM64 交叉编译工具链。
# Qt、FFmpeg 和厂商 SDK 路径由调用方通过 CMake Preset 或环境变量提供，
# 避免把开发者个人路径和特定硬件配置写入仓库。

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc CACHE FILEPATH
    "Linux ARM64 C cross compiler")
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++ CACHE FILEPATH
    "Linux ARM64 C++ cross compiler")

# 厂商 SDK 可以通过同名 CMake 参数覆盖默认 sysroot；环境变量用于命令行临时配置。
if((NOT DEFINED ARM64_SYSROOT OR ARM64_SYSROOT STREQUAL "")
        AND DEFINED ENV{ARM64_SYSROOT}
        AND NOT "$ENV{ARM64_SYSROOT}" STREQUAL "")
    set(ARM64_SYSROOT "$ENV{ARM64_SYSROOT}" CACHE PATH
        "Linux ARM64 target sysroot")
endif()

if(DEFINED ARM64_SYSROOT AND NOT ARM64_SYSROOT STREQUAL "")
    file(TO_CMAKE_PATH "${ARM64_SYSROOT}" RTMP_MONITOR_ARM64_SYSROOT)

    if(NOT IS_DIRECTORY "${RTMP_MONITOR_ARM64_SYSROOT}")
        message(FATAL_ERROR
            "ARM64_SYSROOT does not reference an existing directory: "
            "${RTMP_MONITOR_ARM64_SYSROOT}"
        )
    endif()

    set(CMAKE_SYSROOT "${RTMP_MONITOR_ARM64_SYSROOT}" CACHE PATH
        "Linux ARM64 target sysroot" FORCE)
    list(PREPEND CMAKE_FIND_ROOT_PATH "${RTMP_MONITOR_ARM64_SYSROOT}")
endif()

# moc、rcc 和 uic 等构建工具必须在宿主系统查找；目标头文件、库和
# CMake package 只在目标根中查找，避免把 x86_64 库链接进 ARM64 程序。
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
