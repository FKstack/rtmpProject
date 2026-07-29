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

option(
    RTMP_MONITOR_BUILD_OPENGL_PROTOTYPE
    "Build the optional RGB OpenGL renderer prototype and smoke targets."
    ON
)

if(RTMP_MONITOR_BUILD_OPENGL_PROTOTYPE)
    find_package(Qt6 REQUIRED COMPONENTS OpenGL OpenGLWidgets)

    if(WIN32)
        find_package(OpenGL REQUIRED)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_path(RTMP_MONITOR_EGL_INCLUDE_DIR
            NAMES EGL/egl.h
            REQUIRED
        )
        find_path(RTMP_MONITOR_GLES2_INCLUDE_DIR
            NAMES GLES2/gl2.h
            REQUIRED
        )
        find_library(RTMP_MONITOR_EGL_LIBRARY
            NAMES EGL
            REQUIRED
        )
        find_library(RTMP_MONITOR_GLES2_LIBRARY
            NAMES GLESv2
            REQUIRED
        )

        foreach(arm64_opengl_path IN ITEMS
            "${RTMP_MONITOR_EGL_INCLUDE_DIR}"
            "${RTMP_MONITOR_GLES2_INCLUDE_DIR}"
            "${RTMP_MONITOR_EGL_LIBRARY}"
            "${RTMP_MONITOR_GLES2_LIBRARY}"
        )
            file(TO_CMAKE_PATH
                "${arm64_opengl_path}"
                arm64_opengl_normalized_path
            )
            if(NOT arm64_opengl_normalized_path
                    MATCHES "^${CMAKE_SYSROOT}(/|$)")
                message(FATAL_ERROR
                    "ARM64 OpenGL dependency escaped the target sysroot: "
                    "${arm64_opengl_path}"
                )
            endif()
        endforeach()

        add_library(rtmp_monitor_arm64_egl INTERFACE)
        target_include_directories(rtmp_monitor_arm64_egl
            INTERFACE
                "${RTMP_MONITOR_EGL_INCLUDE_DIR}"
        )
        target_link_libraries(rtmp_monitor_arm64_egl
            INTERFACE
                "${RTMP_MONITOR_EGL_LIBRARY}"
        )

        add_library(rtmp_monitor_arm64_gles2 INTERFACE)
        target_include_directories(rtmp_monitor_arm64_gles2
            INTERFACE
                "${RTMP_MONITOR_GLES2_INCLUDE_DIR}"
        )
        target_link_libraries(rtmp_monitor_arm64_gles2
            INTERFACE
                "${RTMP_MONITOR_GLES2_LIBRARY}"
        )
    endif()
endif()

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

set(RTMP_MONITOR_LOGGING_SOURCES
    include/common/logging/LogTypes.h
    include/common/logging/LogConfiguration.h
    include/common/logging/SensitiveDataSanitizer.h
    include/common/logging/UserMessageTypes.h
    include/common/logging/UserMessageService.h
    include/common/logging/LogManager.h
    src/common/logging/LogConfiguration.cpp
    src/common/logging/SensitiveDataSanitizer.cpp
    src/common/logging/UserMessageService.cpp
    src/common/logging/LogManager.cpp
)

qt_add_library(rtmp_monitor_logging STATIC
    ${RTMP_MONITOR_LOGGING_SOURCES}
)

target_include_directories(rtmp_monitor_logging
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)

target_link_libraries(rtmp_monitor_logging
    PUBLIC
        Qt6::Core
)

rtmp_monitor_apply_project_options(rtmp_monitor_logging)

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
    include/common/media/PlaybackTypes.h
    include/common/media/DecodeWorkerPool.h
    src/common/media/DecodeWorkerPool.cpp
    include/common/media/FFmpegPlayer.h
    src/common/media/FFmpegPlayer.cpp
    include/common/media/MultiStreamPlaybackManager.h
    src/common/media/MultiStreamPlaybackManager.cpp
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
    include/common/ui/ConnectionDialog.h
    src/common/ui/ConnectionDialog.cpp
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
    include/common/ui/LogPanel.h
    src/common/ui/LogPanel.cpp
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
        rtmp_monitor_logging
)

rtmp_monitor_apply_project_options(rtmp_monitor_ui)

if(RTMP_MONITOR_BUILD_OPENGL_PROTOTYPE)
    qt_add_library(rtmp_monitor_opengl_prototype STATIC
        include/common/ui/VideoRenderWidget.h
        src/common/ui/VideoRenderWidget.cpp
    )

    target_include_directories(rtmp_monitor_opengl_prototype
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include/common
    )

    target_link_libraries(rtmp_monitor_opengl_prototype
        PUBLIC
            Qt6::OpenGLWidgets
        PRIVATE
            Qt6::OpenGL
    )

    rtmp_monitor_apply_project_options(rtmp_monitor_opengl_prototype)
endif()

qt_add_executable(rtmp_monitor
    src/main.cpp
    include/common/app/StreamConnectionController.h
    src/common/app/StreamConnectionController.cpp
    ${RTMP_MONITOR_STYLE_RESOURCE}
)

if(WIN32)
    set_target_properties(rtmp_monitor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(rtmp_monitor
    PRIVATE
        rtmp_monitor_app
        rtmp_monitor_logging
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

    if(RTMP_MONITOR_BUILD_OPENGL_PROTOTYPE)
        qt_add_executable(rtmp_monitor_qt_opengl_smoke
            tests/QtOpenGLSmoke.cpp
        )
        target_link_libraries(rtmp_monitor_qt_opengl_smoke
            PRIVATE
                rtmp_monitor_opengl_prototype
        )
        rtmp_monitor_apply_project_options(rtmp_monitor_qt_opengl_smoke)

        if(WIN32)
            set_target_properties(rtmp_monitor_qt_opengl_smoke
                PROPERTIES WIN32_EXECUTABLE FALSE)

            add_executable(rtmp_monitor_opengl_windows_smoke
                tests/OpenGLWindowsSmoke.cpp
            )
            target_link_libraries(rtmp_monitor_opengl_windows_smoke
                PRIVATE
                    OpenGL::GL
                    gdi32
                    user32
            )
            rtmp_monitor_apply_project_options(
                rtmp_monitor_opengl_windows_smoke
            )

            add_test(
                NAME rtmp_monitor_opengl_windows_smoke
                COMMAND rtmp_monitor_opengl_windows_smoke
            )
            add_test(
                NAME rtmp_monitor_qt_opengl_smoke
                COMMAND rtmp_monitor_qt_opengl_smoke
            )
            set_tests_properties(
                rtmp_monitor_opengl_windows_smoke
                rtmp_monitor_qt_opengl_smoke
                PROPERTIES
                    LABELS "week6;opengl"
                    RUN_SERIAL TRUE
                    TIMEOUT 15
            )
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_executable(rtmp_monitor_opengl_egl_smoke
                tests/OpenGLEglSmoke.cpp
            )
            target_link_libraries(rtmp_monitor_opengl_egl_smoke
                PRIVATE
                    rtmp_monitor_arm64_egl
                    rtmp_monitor_arm64_gles2
            )
            rtmp_monitor_apply_project_options(
                rtmp_monitor_opengl_egl_smoke
            )
        endif()
    endif()

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

    qt_add_executable(rtmp_monitor_multi_stream_test
        tests/MultiStreamPlaybackManagerTest.cpp
    )

    if(WIN32)
        set_target_properties(rtmp_monitor_multi_stream_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()

    target_link_libraries(rtmp_monitor_multi_stream_test
        PRIVATE
            rtmp_monitor_media
            Qt6::Test
    )

    rtmp_monitor_apply_project_options(rtmp_monitor_multi_stream_test)
    add_test(NAME rtmp_monitor_multi_stream_test
        COMMAND rtmp_monitor_multi_stream_test)
    rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_multi_stream_test)

    qt_add_executable(rtmp_monitor_logging_test
        tests/LogManagerTest.cpp
    )
    if(WIN32)
        set_target_properties(rtmp_monitor_logging_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    target_link_libraries(rtmp_monitor_logging_test
        PRIVATE
            rtmp_monitor_logging
            Qt6::Test
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_logging_test)
    add_test(NAME rtmp_monitor_logging_test
        COMMAND rtmp_monitor_logging_test)

    qt_add_executable(rtmp_monitor_user_message_test
        tests/UserMessageServiceTest.cpp
    )
    if(WIN32)
        set_target_properties(rtmp_monitor_user_message_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    target_link_libraries(rtmp_monitor_user_message_test
        PRIVATE
            rtmp_monitor_logging
            Qt6::Test
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_user_message_test)
    add_test(NAME rtmp_monitor_user_message_test
        COMMAND rtmp_monitor_user_message_test)

    qt_add_executable(rtmp_monitor_connection_controller_test
        tests/StreamConnectionControllerTest.cpp
        include/common/app/StreamConnectionController.h
        src/common/app/StreamConnectionController.cpp
        ${RTMP_MONITOR_STYLE_RESOURCE}
    )
    if(WIN32)
        set_target_properties(rtmp_monitor_connection_controller_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    target_link_libraries(rtmp_monitor_connection_controller_test
        PRIVATE
            rtmp_monitor_logging
            rtmp_monitor_media
            rtmp_monitor_ui
            Qt6::Test
    )
    rtmp_monitor_apply_project_options(
        rtmp_monitor_connection_controller_test
    )
    add_test(NAME rtmp_monitor_connection_controller_test
        COMMAND rtmp_monitor_connection_controller_test)
    rtmp_monitor_copy_ffmpeg_runtime(
        rtmp_monitor_connection_controller_test
    )

    qt_add_executable(rtmp_monitor_log_panel_test
        tests/LogPanelTest.cpp
        ${RTMP_MONITOR_STYLE_RESOURCE}
    )
    if(WIN32)
        set_target_properties(rtmp_monitor_log_panel_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    target_link_libraries(rtmp_monitor_log_panel_test
        PRIVATE
            rtmp_monitor_app
            rtmp_monitor_ui
            Qt6::Test
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_log_panel_test)
    add_test(NAME rtmp_monitor_log_panel_test
        COMMAND rtmp_monitor_log_panel_test)
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
