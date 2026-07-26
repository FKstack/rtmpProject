cmake_minimum_required(VERSION 3.21)

project(RtmpMonitor
    VERSION 0.1.0
    DESCRIPTION "Cross-platform multi-stream RTMP monitor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    if(NOT MSVC)
        message(FATAL_ERROR
            "Windows x86_64 builds require MSVC and a matching Qt 6 MSVC kit."
        )
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" RTMP_MONITOR_SYSTEM_PROCESSOR)
    if(NOT RTMP_MONITOR_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        message(FATAL_ERROR
            "Linux builds currently target ARM64 only. "
            "Detected processor: ${CMAKE_SYSTEM_PROCESSOR}"
        )
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        message(FATAL_ERROR
            "Linux ARM64 builds require GCC or Clang. "
            "Detected compiler: ${CMAKE_CXX_COMPILER_ID}"
        )
    endif()
else()
    message(FATAL_ERROR
        "Unsupported target platform: ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}. "
        "Supported targets are Windows x86_64 with MSVC and Linux ARM64 with GCC/Clang."
    )
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(rtmp_monitor_ffmpeg INTERFACE)

if(WIN32)
    find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL SWSCALE)
    target_link_libraries(rtmp_monitor_ffmpeg INTERFACE
        FFmpeg::avformat
        FFmpeg::avcodec
        FFmpeg::avutil
        FFmpeg::swscale
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RTMP_MONITOR_FFMPEG REQUIRED IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
        libswscale
    )
    target_link_libraries(rtmp_monitor_ffmpeg INTERFACE PkgConfig::RTMP_MONITOR_FFMPEG)
endif()

if(WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    if(QT_QMAKE_EXECUTABLE MATCHES "[/\\\\]mingw")
        message(FATAL_ERROR
            "Detected a MinGW Qt kit at: ${QT_QMAKE_EXECUTABLE}\n"
            "Windows builds require an MSVC Qt 6 kit. Configure CMAKE_PREFIX_PATH "
            "with a kit such as E:/QT6/6.x.x/msvc2022_64."
        )
    endif()
endif()

# Qt 6.2 尚未提供该便利函数；上方已显式启用 AUTOMOC、AUTOUIC 和 AUTORCC，
# 因此旧版 Qt 可以安全跳过，较新版 Qt 继续采用其标准工程默认值。
if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
endif()

set(RTMP_MONITOR_APP_SOURCES
    include/common/core/Singleton.h
    include/common/app/StyleLoader.h
    src/common/app/StyleLoader.cpp
)

set(RTMP_MONITOR_STYLE_RESOURCE
    resources/styles.qrc
)

qt_add_library(rtmp_monitor_app STATIC
    ${RTMP_MONITOR_APP_SOURCES}
)

target_include_directories(rtmp_monitor_app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)

target_link_libraries(rtmp_monitor_app
    PUBLIC
        Qt6::Widgets
)

rtmp_monitor_apply_project_options(rtmp_monitor_app)

set(RTMP_MONITOR_MEDIA_SOURCES
    include/common/media/FFmpegPlayer.h
    src/common/media/FFmpegPlayer.cpp
)

qt_add_library(rtmp_monitor_media STATIC
    ${RTMP_MONITOR_MEDIA_SOURCES}
)

target_include_directories(rtmp_monitor_media
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)

target_link_libraries(rtmp_monitor_media
    PUBLIC
        Qt6::Gui
    PRIVATE
        rtmp_monitor_ffmpeg
)

rtmp_monitor_apply_project_options(rtmp_monitor_media)

# UI 作为独立静态库供主程序和测试复用，避免重复编译相同实现。
set(RTMP_MONITOR_UI_SOURCES
    include/common/ui/FullscreenControlBar.h
    src/common/ui/FullscreenControlBar.cpp
    include/common/ui/FullscreenVideoWindow.h
    src/common/ui/FullscreenVideoWindow.cpp
    include/common/ui/VideoWidget.h
    src/common/ui/VideoWidget.cpp
    include/common/ui/VideoGridWidget.h
    src/common/ui/VideoGridWidget.cpp
    include/common/ui/MainWindow.h
    src/common/ui/MainWindow.cpp
)

qt_add_library(rtmp_monitor_ui STATIC
    ${RTMP_MONITOR_UI_SOURCES}
)

target_include_directories(rtmp_monitor_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)

target_link_libraries(rtmp_monitor_ui
    PUBLIC
        Qt6::Widgets
)

rtmp_monitor_apply_project_options(rtmp_monitor_ui)

qt_add_executable(rtmp_monitor
    src/main.cpp
    ${RTMP_MONITOR_STYLE_RESOURCE}
)

if(WIN32)
    set_target_properties(rtmp_monitor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(rtmp_monitor
    PRIVATE
        rtmp_monitor_app
        rtmp_monitor_media
        rtmp_monitor_ui
)

rtmp_monitor_apply_project_options(rtmp_monitor)

function(rtmp_monitor_copy_ffmpeg_runtime target_name)
    if(NOT WIN32)
        return()
    endif()
    if(NOT DEFINED VCPKG_INSTALLED_DIR OR VCPKG_INSTALLED_DIR STREQUAL "" OR
       NOT DEFINED VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET STREQUAL "")
        message(FATAL_ERROR
            "Windows FFmpeg runtime deployment requires VCPKG_INSTALLED_DIR "
            "and VCPKG_TARGET_TRIPLET. Configure with the vcpkg toolchain."
        )
    endif()

    set(ffmpeg_release_bin
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    set(ffmpeg_debug_bin
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")

    foreach(ffmpeg_dll IN ITEMS
        avformat-62.dll
        avcodec-62.dll
        avutil-60.dll
        swscale-9.dll
    )
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<IF:$<CONFIG:Debug>,${ffmpeg_debug_bin}/${ffmpeg_dll},${ffmpeg_release_bin}/${ffmpeg_dll}>"
                "$<TARGET_FILE_DIR:${target_name}>/${ffmpeg_dll}"
            VERBATIM
        )
    endforeach()
endfunction()

rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor)

include(CTest)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    # 冒烟测试只链接 UI 库，避免意外依赖后续播放器或网络模块。
    qt_add_executable(rtmp_monitor_ui_smoke_test
        tests/VideoGridSmokeTest.cpp
        ${RTMP_MONITOR_STYLE_RESOURCE}
    )

    if(WIN32)
        # 测试使用控制台子系统，便于 CTest 捕获失败诊断。
        set_target_properties(rtmp_monitor_ui_smoke_test PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()

    target_link_libraries(rtmp_monitor_ui_smoke_test
        PRIVATE
            rtmp_monitor_app
            rtmp_monitor_ui
    )

    rtmp_monitor_apply_project_options(rtmp_monitor_ui_smoke_test)

    add_test(NAME rtmp_monitor_ui_smoke_test COMMAND rtmp_monitor_ui_smoke_test)

    qt_add_executable(rtmp_monitor_dynamic_grid_test
        tests/VideoGridDynamicTest.cpp
        ${RTMP_MONITOR_STYLE_RESOURCE}
    )

    if(WIN32)
        set_target_properties(rtmp_monitor_dynamic_grid_test PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()

    target_link_libraries(rtmp_monitor_dynamic_grid_test
        PRIVATE
            rtmp_monitor_ui
            Qt6::Test
    )

    rtmp_monitor_apply_project_options(rtmp_monitor_dynamic_grid_test)

    add_test(NAME rtmp_monitor_dynamic_grid_test COMMAND rtmp_monitor_dynamic_grid_test)

    qt_add_executable(rtmp_monitor_ffmpeg_player_test
        tests/FFmpegPlayerLifecycleTest.cpp
    )

    if(WIN32)
        set_target_properties(rtmp_monitor_ffmpeg_player_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()

    target_link_libraries(rtmp_monitor_ffmpeg_player_test
        PRIVATE
            rtmp_monitor_media
            Qt6::Test
    )

    rtmp_monitor_apply_project_options(rtmp_monitor_ffmpeg_player_test)
    add_test(NAME rtmp_monitor_ffmpeg_player_test
        COMMAND rtmp_monitor_ffmpeg_player_test)
    rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_ffmpeg_player_test)
endif()

include(GNUInstallDirs)

add_custom_command(TARGET rtmp_monitor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:rtmp_monitor>/styles"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/styles/app.qss"
        "$<TARGET_FILE_DIR:rtmp_monitor>/styles/app.qss"
    VERBATIM
)

install(TARGETS rtmp_monitor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES resources/styles/app.qss
    DESTINATION ${CMAKE_INSTALL_BINDIR}/styles
)
