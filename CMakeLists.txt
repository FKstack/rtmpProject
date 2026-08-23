cmake_minimum_required(VERSION 3.21)

project(RtmpMonitor
    VERSION 0.1.0
    DESCRIPTION "Cross-platform multi-stream RTMP monitor"
    LANGUAGES CXX
)

# CMake 的 project(VERSION) 只接受数字版本；预发布标识在这里集中冻结，
# 并通过生成头传给应用，避免分支名、文档和 --version 输出各自维护。
set(RTMP_MONITOR_PRERELEASE "alpha.1")
set(RTMP_MONITOR_VERSION
    "${PROJECT_VERSION}-${RTMP_MONITOR_PRERELEASE}"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    # CMake's MSVC probe decodes /showIncludes using the active console code
    # page.  On Windows installations where cl.exe emits UTF-8 while that code
    # page is still GBK, the prefix is mojibake and Ninja silently records no
    # header dependencies ("#deps 0").  Preserve the compiler's raw bytes so
    # the prefix in rules.ninja exactly matches subsequent compiler output.
    set(RTMP_MONITOR_SHOWINCLUDES_DIR
        "${CMAKE_BINARY_DIR}/CMakeFiles/RtmpMonitorShowIncludes")
    file(MAKE_DIRECTORY "${RTMP_MONITOR_SHOWINCLUDES_DIR}")
    file(WRITE "${RTMP_MONITOR_SHOWINCLUDES_DIR}/probe.h" "\n")
    file(WRITE "${RTMP_MONITOR_SHOWINCLUDES_DIR}/probe.cpp"
        "#include \"probe.h\"\nint main() { return 0; }\n")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c probe.cpp
        WORKING_DIRECTORY "${RTMP_MONITOR_SHOWINCLUDES_DIR}"
        OUTPUT_VARIABLE RTMP_MONITOR_SHOWINCLUDES_OUTPUT
        ERROR_VARIABLE RTMP_MONITOR_SHOWINCLUDES_ERROR
        RESULT_VARIABLE RTMP_MONITOR_SHOWINCLUDES_RESULT
        ENCODING NONE
    )
    if(RTMP_MONITOR_SHOWINCLUDES_RESULT EQUAL 0 AND
       RTMP_MONITOR_SHOWINCLUDES_OUTPUT MATCHES
       "(^|\n)([^:\n][^:\n]+:[^:\n]*[^: \n][^: \n]:?[ \t]+)([A-Za-z]:\\\\|\\./|\\.\\\\|/)")
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
        message(STATUS "Using raw MSVC /showIncludes prefix for Ninja dependency tracking")
    else()
        message(FATAL_ERROR
            "Unable to determine the raw MSVC /showIncludes prefix. "
            "Ninja header dependency tracking would be unsafe. Compiler output: "
            "${RTMP_MONITOR_SHOWINCLUDES_OUTPUT}${RTMP_MONITOR_SHOWINCLUDES_ERROR}")
    endif()
endif()

if(WIN32)
    if(NOT MSVC)
        message(FATAL_ERROR
            "Windows x86_64 builds require MSVC and a matching Qt 6 MSVC kit."
        )
    endif()

    # Visual Studio does not inject the vcpkg toolchain when CMake Presets are
    # enabled. Fail before Qt/FFmpeg discovery so a broken preset produces an
    # actionable diagnostic instead of a secondary missing build.ninja error.
    set(RTMP_MONITOR_HAS_MANUAL_FFMPEG_HINT FALSE)
    foreach(ffmpeg_hint IN ITEMS
        FFMPEG_INCLUDE_DIRS
        FFMPEG_LIBRARIES
        FFmpeg_DIR
    )
        if(DEFINED ${ffmpeg_hint} AND NOT "${${ffmpeg_hint}}" STREQUAL "")
            set(RTMP_MONITOR_HAS_MANUAL_FFMPEG_HINT TRUE)
        endif()
    endforeach()
    if(NOT RTMP_MONITOR_HAS_MANUAL_FFMPEG_HINT)
        if(NOT DEFINED CMAKE_TOOLCHAIN_FILE OR
           CMAKE_TOOLCHAIN_FILE STREQUAL "")
            message(FATAL_ERROR
                "Windows FFmpeg discovery requires the vcpkg toolchain. "
                "Set VCPKG_ROOT and use the Qt-Debug/Qt-Release preset, or "
                "configure with "
                "-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake. "
                "In Visual Studio select Qt-Debug, then Delete Cache and "
                "Reconfigure."
            )
        elseif(NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
            message(FATAL_ERROR
                "CMAKE_TOOLCHAIN_FILE does not exist: ${CMAKE_TOOLCHAIN_FILE}. "
                "Check VCPKG_ROOT in the ignored CMakeUserPresets.json, then "
                "Delete Cache and Reconfigure in Visual Studio."
            )
        endif()
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

option(RTMP_MONITOR_ENABLE_WEBRTC
    "Build WebRTC developer probes and the staged WebRTC V2 client." OFF)

if(RTMP_MONITOR_ENABLE_WEBRTC)
    find_package(LibDataChannel 0.24.5 EXACT CONFIG QUIET)
    if(NOT LibDataChannel_FOUND)
        message(FATAL_ERROR
            "RTMP_MONITOR_ENABLE_WEBRTC=ON requires libdatachannel 0.24.5. "
            "Install the matching target architecture and reconfigure."
        )
    endif()
    if(NOT TARGET LibDataChannel::LibDataChannel)
        message(FATAL_ERROR
            "libdatachannel 0.24.5 was found without the required "
            "LibDataChannel::LibDataChannel imported target."
        )
    endif()
endif()

find_package(eclipse-paho-mqtt-c CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Linux 渲染构建模式：RASTER（纯 CPU，无 GL 依赖）/ GLES3（必须含 ES3 依赖）/
# AUTO（可选探测 GL 依赖，齐全则双后端，否则退化为 RASTER-only）。
# Windows 始终包含 Desktop GL 后端，行为与既有版本一致。
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(RTMP_MONITOR_LINUX_RENDER_MODE "AUTO" CACHE STRING
        "Linux renderer build: AUTO, RASTER, or GLES3")
    set_property(CACHE RTMP_MONITOR_LINUX_RENDER_MODE
        PROPERTY STRINGS AUTO RASTER GLES3)
    if(NOT RTMP_MONITOR_LINUX_RENDER_MODE MATCHES "^(AUTO|RASTER|GLES3)$")
        message(FATAL_ERROR
            "RTMP_MONITOR_LINUX_RENDER_MODE must be AUTO, RASTER or GLES3, "
            "got: ${RTMP_MONITOR_LINUX_RENDER_MODE}"
        )
    endif()
endif()

set(RTMP_MONITOR_HAS_OPENGL 0)
if(WIN32)
    set(RTMP_MONITOR_HAS_OPENGL 1)
    set(RTMP_MONITOR_LINUX_RENDER_MODE_EFFECTIVE "DESKTOP")
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Network Multimedia)
    find_package(OpenGL REQUIRED)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(RTMP_MONITOR_LINUX_RENDER_MODE_EFFECTIVE
        "${RTMP_MONITOR_LINUX_RENDER_MODE}")
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia)

    if(NOT RTMP_MONITOR_LINUX_RENDER_MODE STREQUAL "RASTER")
        # AUTO 使用 QUIET 探测；GLES3 使用 REQUIRED 强制成功。
        set(RTMP_MONITOR_GL_FIND_QUIET QUIET)
        set(RTMP_MONITOR_GL_FIND_REQUIRED OFF)
        if(RTMP_MONITOR_LINUX_RENDER_MODE STREQUAL "GLES3")
            set(RTMP_MONITOR_GL_FIND_QUIET)
            set(RTMP_MONITOR_GL_FIND_REQUIRED ON)
        endif()

        find_package(Qt6 ${RTMP_MONITOR_GL_FIND_QUIET} COMPONENTS
            OpenGL OpenGLWidgets)
        find_path(RTMP_MONITOR_EGL_INCLUDE_DIR NAMES EGL/egl.h)
        find_path(RTMP_MONITOR_GLES3_INCLUDE_DIR NAMES GLES3/gl3.h)
        find_library(RTMP_MONITOR_EGL_LIBRARY NAMES EGL)
        find_library(RTMP_MONITOR_GLES3_LIBRARY NAMES GLESv2)

        set(RTMP_MONITOR_GL_DEPENDENCIES_OK TRUE)
        foreach(dependency IN ITEMS
            "${RTMP_MONITOR_EGL_INCLUDE_DIR}"
            "${RTMP_MONITOR_GLES3_INCLUDE_DIR}"
            "${RTMP_MONITOR_EGL_LIBRARY}"
            "${RTMP_MONITOR_GLES3_LIBRARY}"
        )
            if(dependency MATCHES "-NOTFOUND$" OR dependency STREQUAL "")
                set(RTMP_MONITOR_GL_DEPENDENCIES_OK FALSE)
            endif()
        endforeach()
        if(NOT Qt6OpenGL_FOUND OR NOT Qt6OpenGLWidgets_FOUND)
            set(RTMP_MONITOR_GL_DEPENDENCIES_OK FALSE)
        endif()

        if(RTMP_MONITOR_GL_DEPENDENCIES_OK)
            set(RTMP_MONITOR_HAS_OPENGL 1)
        elseif(RTMP_MONITOR_GL_FIND_REQUIRED)
            message(FATAL_ERROR
                "GLES3 build mode requires ARM64 Qt OpenGL/OpenGLWidgets and "
                "EGL/GLESv2 inside the target sysroot. Missing: "
                "EGL include='${RTMP_MONITOR_EGL_INCLUDE_DIR}' "
                "GLES3 include='${RTMP_MONITOR_GLES3_INCLUDE_DIR}' "
                "EGL lib='${RTMP_MONITOR_EGL_LIBRARY}' "
                "GLESv2 lib='${RTMP_MONITOR_GLES3_LIBRARY}' "
                "Qt6OpenGL=${Qt6OpenGL_FOUND} "
                "Qt6OpenGLWidgets=${Qt6OpenGLWidgets_FOUND}"
            )
        else()
            set(RTMP_MONITOR_LINUX_RENDER_MODE_EFFECTIVE "RASTER")
            message(STATUS
                "AUTO mode: GL dependencies not all available; "
                "building RASTER-only (CPU) backends."
            )
        endif()
    endif()

    if(RTMP_MONITOR_HAS_OPENGL)
        foreach(arm64_opengl_path IN ITEMS
            "${RTMP_MONITOR_EGL_INCLUDE_DIR}"
            "${RTMP_MONITOR_GLES3_INCLUDE_DIR}"
            "${RTMP_MONITOR_EGL_LIBRARY}"
            "${RTMP_MONITOR_GLES3_LIBRARY}"
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

        add_library(rtmp_monitor_arm64_gles3 INTERFACE)
        target_include_directories(rtmp_monitor_arm64_gles3
            INTERFACE
                "${RTMP_MONITOR_GLES3_INCLUDE_DIR}"
        )
        target_link_libraries(rtmp_monitor_arm64_gles3
            INTERFACE
                "${RTMP_MONITOR_GLES3_LIBRARY}"
        )
    endif()
endif()

# 编译期渲染能力配置头；业务代码只使用该头暴露的能力宏。
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RtmpMonitorBuildConfig.h.in"
    "${CMAKE_BINARY_DIR}/generated/RtmpMonitorBuildConfig.h"
    @ONLY
)

option(
    RTMP_MONITOR_BUILD_OPENGL_PROTOTYPE
    "Build the optional RGB OpenGL renderer prototype and smoke targets."
    ON
)

if(RTMP_MONITOR_BUILD_OPENGL_PROTOTYPE AND NOT RTMP_MONITOR_HAS_OPENGL)
    message(STATUS
        "OpenGL prototype disabled: this build has no OpenGL backend."
    )
    set(RTMP_MONITOR_BUILD_OPENGL_PROTOTYPE OFF)
endif()

add_library(rtmp_monitor_ffmpeg INTERFACE)

if(WIN32)
    find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL SWSCALE SWRESAMPLE)
    target_link_libraries(rtmp_monitor_ffmpeg INTERFACE
        FFmpeg::avformat
        FFmpeg::avcodec
        FFmpeg::avutil
        FFmpeg::swscale
        FFmpeg::swresample
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RTMP_MONITOR_FFMPEG REQUIRED IMPORTED_TARGET
        libavformat
        libavcodec
        libavutil
        libswscale
        libswresample
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

add_library(rtmp_monitor_webrtc_contracts INTERFACE)
target_sources(rtmp_monitor_webrtc_contracts INTERFACE
    include/common/webrtc_contracts/WebRtcSessionContracts.h)
target_include_directories(rtmp_monitor_webrtc_contracts INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/common)

if(RTMP_MONITOR_ENABLE_WEBRTC)
    # The Week 2 developer probe remains isolated from the Week 4 transport.
    qt_add_library(rtmp_monitor_webrtc_signaling STATIC
        include/common/webrtc_dev/SessionPackage.h
        src/common/webrtc_dev/SessionPackage.cpp
    )
    target_include_directories(rtmp_monitor_webrtc_signaling
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/common)
    target_link_libraries(rtmp_monitor_webrtc_signaling PUBLIC Qt6::Core)
    set_target_properties(rtmp_monitor_webrtc_signaling PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/webrtc/lib/$<CONFIG>"
        COMPILE_PDB_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/webrtc/lib/$<CONFIG>")
    if(WIN32)
        target_link_libraries(rtmp_monitor_webrtc_signaling PRIVATE advapi32)
    endif()
    rtmp_monitor_apply_project_options(rtmp_monitor_webrtc_signaling)

    qt_add_library(rtmp_monitor_webrtc_probe_core STATIC
        include/common/webrtc_dev/PeerConnectionProbe.h
        include/common/webrtc_dev/LoopbackExchange.h
        src/common/webrtc_dev/PeerConnectionProbe.cpp
        src/common/webrtc_dev/LoopbackExchange.cpp
    )
    target_include_directories(rtmp_monitor_webrtc_probe_core
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/common)
    target_link_libraries(rtmp_monitor_webrtc_probe_core
        PUBLIC
            rtmp_monitor_webrtc_signaling
        PRIVATE
            LibDataChannel::LibDataChannel
    )
    set_target_properties(rtmp_monitor_webrtc_probe_core PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/webrtc/lib/$<CONFIG>"
        COMPILE_PDB_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/webrtc/lib/$<CONFIG>")
    rtmp_monitor_apply_project_options(rtmp_monitor_webrtc_probe_core)

    qt_add_executable(rtmp_monitor_webrtc_probe
        src/tools/WebRtcProbeMain.cpp)
    set_target_properties(rtmp_monitor_webrtc_probe
        PROPERTIES
            WIN32_EXECUTABLE FALSE
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/webrtc/$<CONFIG>")
    target_link_libraries(rtmp_monitor_webrtc_probe
        PRIVATE rtmp_monitor_webrtc_probe_core Qt6::Core)
    rtmp_monitor_apply_project_options(rtmp_monitor_webrtc_probe)

    function(rtmp_monitor_copy_webrtc_runtime target_name)
        if(NOT WIN32)
            return()
        endif()
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target_name}>
                $<TARGET_FILE_DIR:${target_name}>
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endfunction()
    rtmp_monitor_copy_webrtc_runtime(rtmp_monitor_webrtc_probe)
endif()

# Protocol-neutral, header-only encoded media contract. Future publisher,
# transport and media targets may depend on it without depending on each other.
add_library(rtmp_monitor_h264_contracts INTERFACE)
target_sources(rtmp_monitor_h264_contracts INTERFACE
    include/common/h264/H264MediaContracts.h
)
target_include_directories(rtmp_monitor_h264_contracts INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)

if(RTMP_MONITOR_ENABLE_WEBRTC)
    add_library(rtmp_monitor_webrtc_transport STATIC
        include/common/webrtc_transport/WebRtcEndpointSession.h
        src/common/webrtc_transport/WebRtcEndpointSession.cpp
    )
    target_include_directories(rtmp_monitor_webrtc_transport
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/common)
    target_link_libraries(rtmp_monitor_webrtc_transport
        PUBLIC
            rtmp_monitor_h264_contracts
            rtmp_monitor_webrtc_contracts
        PRIVATE
            LibDataChannel::LibDataChannel
    )
    set_target_properties(rtmp_monitor_webrtc_transport PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/webrtc/lib/$<CONFIG>"
        COMPILE_PDB_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/webrtc/lib/$<CONFIG>")
    rtmp_monitor_apply_project_options(rtmp_monitor_webrtc_transport)

    add_library(rtmp_monitor_h264_publisher_source STATIC
        include/common/publisher/Mp4H264PublisherSource.h
        src/common/publisher/Mp4H264PublisherSource.cpp
    )
    target_include_directories(rtmp_monitor_h264_publisher_source
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include/common)
    target_link_libraries(rtmp_monitor_h264_publisher_source
        PUBLIC rtmp_monitor_h264_contracts
        PRIVATE rtmp_monitor_ffmpeg)
    set_target_properties(rtmp_monitor_h264_publisher_source PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/webrtc/lib/$<CONFIG>"
        COMPILE_PDB_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/webrtc/lib/$<CONFIG>")
    rtmp_monitor_apply_project_options(rtmp_monitor_h264_publisher_source)
endif()

# 媒体服务器边界库：只读配置、URL 生成和异步健康观察，
# 不包含任何服务器进程管理能力。
set(RTMP_MONITOR_SERVER_SOURCES
    include/common/server/MediaServerTypes.h
    include/common/server/RtmpUrlBuilder.h
    src/common/server/RtmpUrlBuilder.cpp
    include/common/server/MediaServerConfiguration.h
    src/common/server/MediaServerConfiguration.cpp
    include/common/server/MediaServerMonitor.h
    src/common/server/MediaServerMonitor.cpp
)

qt_add_library(rtmp_monitor_server STATIC
    ${RTMP_MONITOR_SERVER_SOURCES}
)

target_include_directories(rtmp_monitor_server
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)

target_link_libraries(rtmp_monitor_server
    PUBLIC
        Qt6::Core
        Qt6::Network
)

rtmp_monitor_apply_project_options(rtmp_monitor_server)

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

qt_add_library(rtmp_monitor_profiles STATIC
    include/common/profiles/SavedStreamProfile.h
    include/common/profiles/SavedStreamRepository.h
    src/common/profiles/SavedStreamRepository.cpp
)
target_include_directories(rtmp_monitor_profiles PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)
target_link_libraries(rtmp_monitor_profiles PUBLIC Qt6::Core)
rtmp_monitor_apply_project_options(rtmp_monitor_profiles)

qt_add_library(rtmp_monitor_control_policy STATIC
    include/common/control_policy/ControlSessionGuard.h
    src/common/control_policy/ControlSessionGuard.cpp
)
target_include_directories(rtmp_monitor_control_policy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)
target_link_libraries(rtmp_monitor_control_policy PUBLIC Qt6::Core)
rtmp_monitor_apply_project_options(rtmp_monitor_control_policy)

qt_add_library(rtmp_monitor_event_center STATIC
    include/common/event_center/EventCenterTypes.h
    src/common/event_center/EventCenterTypes.cpp
    include/common/event_center/EventCenterStore.h
    src/common/event_center/EventCenterStore.cpp
    include/common/event_center/EventCenterService.h
    src/common/event_center/EventCenterService.cpp
)
target_include_directories(rtmp_monitor_event_center PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)
target_link_libraries(rtmp_monitor_event_center PUBLIC Qt6::Core)
rtmp_monitor_apply_project_options(rtmp_monitor_event_center)

qt_add_library(rtmp_monitor_evidence STATIC
    include/common/evidence/EvidenceTypes.h
    src/common/evidence/EvidenceTypes.cpp
    include/common/evidence/AtomicPngWriter.h
    src/common/evidence/AtomicPngWriter.cpp
    include/common/evidence/EvidenceCatalogStore.h
    src/common/evidence/EvidenceCatalogStore.cpp
    include/common/evidence/EvidenceService.h
    src/common/evidence/EvidenceService.cpp
)
target_include_directories(rtmp_monitor_evidence PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)
target_link_libraries(rtmp_monitor_evidence PUBLIC Qt6::Core Qt6::Gui)
rtmp_monitor_apply_project_options(rtmp_monitor_evidence)

qt_add_library(rtmp_monitor_device_control STATIC
    include/common/device_control/DeviceControlTypes.h
    include/common/device_control/DeviceCommandCodec.h
    src/common/device_control/DeviceCommandCodec.cpp
    include/common/device_control/DeviceIdentity.h
    src/common/device_control/DeviceIdentity.cpp
    include/common/device_control/DeviceHeartbeatCodec.h
    src/common/device_control/DeviceHeartbeatCodec.cpp
    include/common/device_control/DevicePresenceTracker.h
    src/common/device_control/DevicePresenceTracker.cpp
    include/common/device_control/MqttSettingsRepository.h
    src/common/device_control/MqttSettingsRepository.cpp
    include/common/device_control/MqttDeviceClient.h
    src/common/device_control/MqttDeviceClient.cpp
)
target_include_directories(rtmp_monitor_device_control PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)
target_link_libraries(rtmp_monitor_device_control
    PUBLIC
        Qt6::Core
        eclipse-paho-mqtt-c::paho-mqtt3a
)
rtmp_monitor_apply_project_options(rtmp_monitor_device_control)

set(RTMP_MONITOR_MEDIA_SOURCES
    include/common/media/EncodedVideoDecodeSession.h
    src/common/media/EncodedVideoDecodeSession.cpp
    src/common/media/EncodedVideoDecodeSessionFfmpegAccess.h
    include/common/media/EncodedVideoInputHandle.h
    src/common/media/EncodedVideoInputHandle.cpp
    src/common/media/EncodedVideoInputControl.h
    include/common/media/LatencyMarkerCodec.h
    src/common/media/LatencyMarkerCodec.cpp
    include/common/media/VideoFrame.h
    src/common/media/VideoFrame.cpp
    include/common/media/LatestFrameMailbox.h
    src/common/media/LatestFrameMailbox.cpp
    include/common/media/VideoFrameConverter.h
    src/common/media/VideoFrameConverter.cpp
    src/common/media/FfmpegVideoFrameAdapter.h
    src/common/media/FfmpegVideoFrameAdapter.cpp
    src/common/media/FfmpegSessionTypes.h
    src/common/media/FfmpegInputSession.h
    src/common/media/FfmpegInputSession.cpp
    include/common/media/PlaybackTypes.h
    include/common/media/AudioPacketSink.h
    include/common/media/AudioPlaybackObserver.h
    include/common/media/AudioPlaybackEngine.h
    src/common/media/AudioPlaybackEngine.cpp
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
        rtmp_monitor_h264_contracts
    PRIVATE
        rtmp_monitor_ffmpeg
        Qt6::Multimedia
)

rtmp_monitor_apply_project_options(rtmp_monitor_media)

set(RTMP_MONITOR_RENDER_SOURCES
    include/common/render/RenderRuntimeMetrics.h
    include/common/render/DisplayFrameRatePolicy.h
    src/common/render/DisplayFrameRatePolicy.cpp
    include/common/render/RenderTypes.h
    src/common/render/RenderTypes.cpp
    include/common/render/VideoRenderController.h
    src/common/render/VideoRenderController.cpp
    include/common/render/EmbeddedGlCapabilities.h
    src/common/render/EmbeddedGlCapabilities.cpp
)

if(RTMP_MONITOR_HAS_OPENGL)
    list(APPEND RTMP_MONITOR_RENDER_SOURCES
        include/common/render/OpenGLGridRenderer.h
        src/common/render/OpenGLGridRenderer.cpp
    )
endif()

qt_add_library(rtmp_monitor_render STATIC
    ${RTMP_MONITOR_RENDER_SOURCES}
)

target_include_directories(rtmp_monitor_render
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/common
    PRIVATE
        ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(rtmp_monitor_render
    PUBLIC
        Qt6::Gui
        rtmp_monitor_media
)

if(RTMP_MONITOR_HAS_OPENGL)
    target_link_libraries(rtmp_monitor_render
        PRIVATE
            Qt6::OpenGL
    )
endif()

rtmp_monitor_apply_project_options(rtmp_monitor_render)

# 诊断组合层：可同时读取 media/render 指标，但两者不反向依赖它。
qt_add_library(rtmp_monitor_diagnostics STATIC
    include/common/diagnostics/RuntimeMetricsReporter.h
    src/common/diagnostics/RuntimeMetricsReporter.cpp
)
target_include_directories(rtmp_monitor_diagnostics
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/common
)
target_link_libraries(rtmp_monitor_diagnostics
    PUBLIC
        Qt6::Core
        rtmp_monitor_media
        rtmp_monitor_render
)
rtmp_monitor_apply_project_options(rtmp_monitor_diagnostics)

# UI 作为独立静态库供主程序和测试复用，避免重复编译相同实现。
set(RTMP_MONITOR_UI_SOURCES
    include/common/ui/ConnectionDialog.h
    src/common/ui/ConnectionDialog.cpp
    include/common/ui/DeviceControlPanel.h
    src/common/ui/DeviceControlPanel.cpp
    include/common/ui/DeviceControlInputRouter.h
    src/common/ui/DeviceControlInputRouter.cpp
    include/common/ui/VirtualJoystickWidget.h
    src/common/ui/VirtualJoystickWidget.cpp
    include/common/ui/MqttSettingsDialog.h
    src/common/ui/MqttSettingsDialog.cpp
    include/common/ui/SavedStreamsDialog.h
    src/common/ui/SavedStreamsDialog.cpp
    include/common/ui/SavedStreamEditorDialog.h
    src/common/ui/SavedStreamEditorDialog.cpp
    include/common/ui/FullscreenControlBar.h
    src/common/ui/FullscreenControlBar.cpp
    include/common/ui/FullscreenChromeController.h
    src/common/ui/FullscreenChromeController.cpp
    include/common/ui/FullscreenScreenshotService.h
    src/common/ui/FullscreenScreenshotService.cpp
    include/common/ui/FullscreenVideoWindow.h
    src/common/ui/FullscreenVideoWindow.cpp
    include/common/ui/VideoWidget.h
    src/common/ui/VideoWidget.cpp
    include/common/ui/MonitoringGridLayout.h
    src/common/ui/MonitoringGridLayout.cpp
    include/common/ui/GridTransitionAnimator.h
    src/common/ui/GridTransitionAnimator.cpp
    include/common/ui/VideoGridSceneBuilder.h
    src/common/ui/VideoGridSceneBuilder.cpp
    include/common/ui/VideoGridWidget.h
    src/common/ui/VideoGridWidget.cpp
    include/common/ui/MainWindow.h
    src/common/ui/MainWindow.cpp
    include/common/ui/LogPanel.h
    src/common/ui/LogPanel.cpp
    include/common/ui/EventCenterPanel.h
    src/common/ui/EventCenterPanel.cpp
    include/common/ui/EventDetailDialog.h
    src/common/ui/EventDetailDialog.cpp
    include/common/ui/CpuVideoCanvas.h
    src/common/ui/CpuVideoCanvas.cpp
    include/common/ui/VideoCanvasHost.h
    src/common/ui/VideoCanvasHost.cpp
)

if(RTMP_MONITOR_HAS_OPENGL)
    list(APPEND RTMP_MONITOR_UI_SOURCES
        include/common/ui/VideoOpenGLCanvas.h
        src/common/ui/VideoOpenGLCanvas.cpp
    )
endif()

qt_add_library(rtmp_monitor_ui STATIC
    ${RTMP_MONITOR_UI_SOURCES}
)

target_include_directories(rtmp_monitor_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/common
    PRIVATE
        ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(rtmp_monitor_ui
    PUBLIC
        Qt6::Widgets
        rtmp_monitor_logging
        rtmp_monitor_render
        rtmp_monitor_profiles
        rtmp_monitor_device_control
        rtmp_monitor_event_center
        rtmp_monitor_evidence
)

if(RTMP_MONITOR_HAS_OPENGL)
    target_link_libraries(rtmp_monitor_ui
        PUBLIC
            Qt6::OpenGLWidgets
    )
endif()

rtmp_monitor_apply_project_options(rtmp_monitor_ui)

# Linux 平台策略：bootstrap / policy / factory，只在 Linux 构建中编译。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    qt_add_library(rtmp_monitor_platform_linux STATIC
        include/platform/linux/LinuxRenderingPolicy.h
        src/platform/linux/LinuxRenderingPolicy.cpp
        include/platform/linux/LinuxApplicationBootstrap.h
        src/platform/linux/LinuxApplicationBootstrap.cpp
        include/platform/linux/LinuxRendererFactory.h
        src/platform/linux/LinuxRendererFactory.cpp
    )

    target_include_directories(rtmp_monitor_platform_linux
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include/common
            ${CMAKE_CURRENT_SOURCE_DIR}/include/platform
        PRIVATE
            ${CMAKE_BINARY_DIR}/generated
    )

    target_link_libraries(rtmp_monitor_platform_linux
        PUBLIC
            Qt6::Widgets
            rtmp_monitor_ui
    )

    rtmp_monitor_apply_project_options(rtmp_monitor_platform_linux)
endif()

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
    include/common/app/ApplicationBootstrap.h
    src/common/app/ApplicationBootstrap.cpp
    include/common/app/ApplicationOptions.h
    src/common/app/ApplicationOptions.cpp
    include/common/app/ConnectionBindingRegistry.h
    src/common/app/ConnectionBindingRegistry.cpp
    include/common/app/ControlMediaObservation.h
    include/common/app/ConnectionEventReporter.h
    src/common/app/ConnectionEventReporter.cpp
    include/common/app/StreamConnectionController.h
    src/common/app/StreamConnectionController.cpp
    include/common/app/SavedStreamController.h
    src/common/app/SavedStreamController.cpp
    include/common/app/DeviceControlController.h
    src/common/app/DeviceControlController.cpp
    include/common/app/DeviceControlTransport.h
    src/common/app/DeviceControlTransport.cpp
    include/common/app/PlatformEventBridge.h
    src/common/app/PlatformEventBridge.cpp
    include/common/app/EvidenceCoordinator.h
    src/common/app/EvidenceCoordinator.cpp
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
        rtmp_monitor_diagnostics
        rtmp_monitor_server
        rtmp_monitor_ui
        rtmp_monitor_profiles
        rtmp_monitor_device_control
        rtmp_monitor_control_policy
        rtmp_monitor_event_center
        rtmp_monitor_evidence
)

target_include_directories(rtmp_monitor
    PRIVATE
        ${CMAKE_BINARY_DIR}/generated
)

if(WIN32)
    target_sources(rtmp_monitor PRIVATE
        include/platform/windows/WindowsWindowAppearance.h
        src/platform/windows/WindowsWindowAppearance.cpp
        resources/windows/rtmp_monitor.rc
    )
    target_include_directories(rtmp_monitor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/platform
    )
    target_link_libraries(rtmp_monitor PRIVATE dwmapi)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(rtmp_monitor
        PRIVATE
            rtmp_monitor_platform_linux
    )
endif()

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
        swresample-6.dll
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

if(RTMP_MONITOR_ENABLE_WEBRTC)
    qt_add_executable(rtmp_monitor_webrtc_client
        src/tools/WebRtcClientMain.cpp)
    set_target_properties(rtmp_monitor_webrtc_client PROPERTIES
        WIN32_EXECUTABLE FALSE
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/webrtc/$<CONFIG>")
    target_link_libraries(rtmp_monitor_webrtc_client
        PRIVATE
            Qt6::Core
            rtmp_monitor_webrtc_signaling
            rtmp_monitor_webrtc_transport
            rtmp_monitor_h264_publisher_source
            LibDataChannel::LibDataChannel)
    rtmp_monitor_apply_project_options(rtmp_monitor_webrtc_client)
    rtmp_monitor_copy_webrtc_runtime(rtmp_monitor_webrtc_client)
    rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_webrtc_client)
endif()

option(RTMP_MONITOR_BUILD_VALIDATION_TOOLS
    "Build developer-only camera qualification tools." ON)

if(WIN32 AND RTMP_MONITOR_BUILD_VALIDATION_TOOLS)
    qt_add_executable(rtmp_monitor_audio_qualification
        src/tools/AudioQualificationMain.cpp
    )
    set_target_properties(rtmp_monitor_audio_qualification
        PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(rtmp_monitor_audio_qualification
        PRIVATE rtmp_monitor_media Qt6::Core Qt6::Multimedia)
    rtmp_monitor_apply_project_options(rtmp_monitor_audio_qualification)
    rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_audio_qualification)

    qt_add_executable(rtmp_monitor_camera_source
        src/tools/CameraSourceMain.cpp
    )
    set_target_properties(rtmp_monitor_camera_source
        # 资格录屏需要 MainWindowHandle 指向参考画面，而不是控制台窗口。
        PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(rtmp_monitor_camera_source
        PRIVATE
            Qt6::Widgets
            rtmp_monitor_media
            rtmp_monitor_ffmpeg
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_camera_source)
    rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_camera_source)

    qt_add_executable(rtmp_monitor_video_analyzer
        src/tools/VideoAnalysisMain.cpp
    )
    set_target_properties(rtmp_monitor_video_analyzer
        PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(rtmp_monitor_video_analyzer
        PRIVATE
            Qt6::Core
            rtmp_monitor_media
            rtmp_monitor_ffmpeg
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_video_analyzer)
    rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_video_analyzer)
endif()

include(CTest)

if(BUILD_TESTING)
    qt_add_executable(rtmp_monitor_event_center_service_test
        tests/EventCenterServiceTest.cpp
    )
    target_link_libraries(rtmp_monitor_event_center_service_test PRIVATE
        Qt6::Test rtmp_monitor_event_center
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_event_center_service_test)
    add_test(NAME rtmp_monitor_event_center_service_test
        COMMAND rtmp_monitor_event_center_service_test)

    qt_add_executable(rtmp_monitor_evidence_catalog_test
        tests/EvidenceCatalogStoreTest.cpp)
    target_link_libraries(rtmp_monitor_evidence_catalog_test PRIVATE
        Qt6::Test rtmp_monitor_evidence)
    rtmp_monitor_apply_project_options(rtmp_monitor_evidence_catalog_test)
    add_test(NAME rtmp_monitor_evidence_catalog_test
        COMMAND rtmp_monitor_evidence_catalog_test)

    qt_add_executable(rtmp_monitor_evidence_service_test
        tests/EvidenceServiceTest.cpp)
    target_link_libraries(rtmp_monitor_evidence_service_test PRIVATE
        Qt6::Test rtmp_monitor_evidence)
    rtmp_monitor_apply_project_options(rtmp_monitor_evidence_service_test)
    add_test(NAME rtmp_monitor_evidence_service_test
        COMMAND rtmp_monitor_evidence_service_test)

    qt_add_executable(rtmp_monitor_platform_event_bridge_test
        tests/PlatformEventBridgeTest.cpp
        include/common/app/PlatformEventBridge.h
        src/common/app/PlatformEventBridge.cpp
        include/common/app/StreamConnectionController.h
        src/common/app/StreamConnectionController.cpp
        src/common/app/ConnectionBindingRegistry.cpp
        src/common/app/ConnectionEventReporter.cpp
    )
    target_link_libraries(rtmp_monitor_platform_event_bridge_test PRIVATE
        Qt6::Test
        rtmp_monitor_event_center
        rtmp_monitor_control_policy
        rtmp_monitor_device_control
        rtmp_monitor_logging
        rtmp_monitor_media
        rtmp_monitor_server
        rtmp_monitor_ui
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_platform_event_bridge_test)
    add_test(NAME rtmp_monitor_platform_event_bridge_test
        COMMAND rtmp_monitor_platform_event_bridge_test)
    rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_platform_event_bridge_test)

    qt_add_executable(rtmp_monitor_event_center_panel_test
        tests/EventCenterPanelTest.cpp
        ${RTMP_MONITOR_STYLE_RESOURCE}
    )
    target_link_libraries(rtmp_monitor_event_center_panel_test PRIVATE
        Qt6::Test rtmp_monitor_app rtmp_monitor_ui rtmp_monitor_event_center
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_event_center_panel_test)
    add_test(NAME rtmp_monitor_event_center_panel_test
        COMMAND rtmp_monitor_event_center_panel_test)

    qt_add_executable(rtmp_monitor_saved_stream_repository_test
        tests/SavedStreamRepositoryTest.cpp
    )
    target_link_libraries(rtmp_monitor_saved_stream_repository_test PRIVATE
        Qt6::Test rtmp_monitor_profiles
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_saved_stream_repository_test)
    add_test(NAME rtmp_monitor_saved_stream_repository_test
        COMMAND rtmp_monitor_saved_stream_repository_test)

    qt_add_executable(rtmp_monitor_device_control_contract_test
        tests/DeviceControlContractTest.cpp
    )
    target_link_libraries(rtmp_monitor_device_control_contract_test PRIVATE
        Qt6::Test rtmp_monitor_device_control
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_device_control_contract_test)
    add_test(NAME rtmp_monitor_device_control_contract_test
        COMMAND rtmp_monitor_device_control_contract_test)

    qt_add_executable(rtmp_monitor_control_session_guard_test
        tests/ControlSessionGuardTest.cpp
    )
    target_link_libraries(rtmp_monitor_control_session_guard_test PRIVATE
        Qt6::Test rtmp_monitor_control_policy
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_control_session_guard_test)
    add_test(NAME rtmp_monitor_control_session_guard_test
        COMMAND rtmp_monitor_control_session_guard_test)

    qt_add_executable(rtmp_monitor_mqtt_device_client_test
        tests/MqttDeviceClientTest.cpp
    )
    target_link_libraries(rtmp_monitor_mqtt_device_client_test PRIVATE
        Qt6::Test Qt6::Network rtmp_monitor_device_control
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_mqtt_device_client_test)
    add_test(NAME rtmp_monitor_mqtt_device_client_test
        COMMAND rtmp_monitor_mqtt_device_client_test)

    qt_add_executable(rtmp_monitor_device_control_panel_test
        tests/DeviceControlPanelTest.cpp
    )
    target_link_libraries(rtmp_monitor_device_control_panel_test PRIVATE
        Qt6::Test rtmp_monitor_ui
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_device_control_panel_test)
    add_test(NAME rtmp_monitor_device_control_panel_test
        COMMAND rtmp_monitor_device_control_panel_test)

    qt_add_executable(rtmp_monitor_virtual_joystick_test
        tests/VirtualJoystickWidgetTest.cpp
    )
    target_link_libraries(rtmp_monitor_virtual_joystick_test PRIVATE
        Qt6::Test rtmp_monitor_ui
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_virtual_joystick_test)
    add_test(NAME rtmp_monitor_virtual_joystick_test
        COMMAND rtmp_monitor_virtual_joystick_test)

    qt_add_executable(rtmp_monitor_device_control_input_test
        tests/DeviceControlInputRouterTest.cpp
    )
    target_link_libraries(rtmp_monitor_device_control_input_test PRIVATE
        Qt6::Test rtmp_monitor_ui
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_device_control_input_test)
    add_test(NAME rtmp_monitor_device_control_input_test
        COMMAND rtmp_monitor_device_control_input_test)

    qt_add_executable(rtmp_monitor_device_control_controller_test
        tests/DeviceControlControllerTest.cpp
        include/common/app/DeviceControlController.h
        src/common/app/DeviceControlController.cpp
        include/common/app/DeviceControlTransport.h
        src/common/app/DeviceControlTransport.cpp
    )
    target_link_libraries(rtmp_monitor_device_control_controller_test PRIVATE
        Qt6::Test
        rtmp_monitor_ui
        rtmp_monitor_device_control
        rtmp_monitor_control_policy
    )
    rtmp_monitor_apply_project_options(
        rtmp_monitor_device_control_controller_test)
    add_test(NAME rtmp_monitor_device_control_controller_test
        COMMAND rtmp_monitor_device_control_controller_test)

    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_package(Python3 QUIET COMPONENTS Interpreter)

    qt_add_executable(rtmp_monitor_h264_contract_test
        tests/H264MediaContractsTest.cpp
    )
    target_link_libraries(rtmp_monitor_h264_contract_test PRIVATE
        Qt6::Test
        rtmp_monitor_h264_contracts
        rtmp_monitor_webrtc_contracts)
    rtmp_monitor_apply_project_options(rtmp_monitor_h264_contract_test)
    add_test(NAME rtmp_monitor_h264_contract_test
        COMMAND rtmp_monitor_h264_contract_test)

    qt_add_executable(rtmp_monitor_encoded_video_decode_test
        tests/EncodedVideoDecodeSessionTest.cpp)
    target_link_libraries(rtmp_monitor_encoded_video_decode_test PRIVATE
        Qt6::Test rtmp_monitor_media)
    rtmp_monitor_apply_project_options(rtmp_monitor_encoded_video_decode_test)
    add_test(NAME rtmp_monitor_encoded_video_decode_test
        COMMAND rtmp_monitor_encoded_video_decode_test)
    rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_encoded_video_decode_test)

    if(RTMP_MONITOR_ENABLE_WEBRTC)
        qt_add_executable(rtmp_monitor_webrtc_session_test
            tests/WebRtcSessionPackageTest.cpp)
        target_link_libraries(rtmp_monitor_webrtc_session_test PRIVATE
            Qt6::Test rtmp_monitor_webrtc_signaling)
        if(WIN32)
            target_link_libraries(rtmp_monitor_webrtc_session_test PRIVATE
                advapi32)
        endif()
        rtmp_monitor_apply_project_options(rtmp_monitor_webrtc_session_test)
        add_test(NAME rtmp_monitor_webrtc_session_test
            COMMAND rtmp_monitor_webrtc_session_test)

        qt_add_executable(rtmp_monitor_webrtc_h264_test
            tests/WebRtcH264ApiTest.cpp)
        target_link_libraries(rtmp_monitor_webrtc_h264_test PRIVATE
            Qt6::Test LibDataChannel::LibDataChannel)
        rtmp_monitor_apply_project_options(rtmp_monitor_webrtc_h264_test)
        add_test(NAME rtmp_monitor_webrtc_h264_test
            COMMAND rtmp_monitor_webrtc_h264_test)

        qt_add_executable(rtmp_monitor_webrtc_loopback_test
            tests/WebRtcLoopbackTest.cpp)
        target_link_libraries(rtmp_monitor_webrtc_loopback_test PRIVATE
            Qt6::Test rtmp_monitor_webrtc_probe_core)
        rtmp_monitor_apply_project_options(rtmp_monitor_webrtc_loopback_test)
        add_test(NAME rtmp_monitor_webrtc_loopback_test
            COMMAND rtmp_monitor_webrtc_loopback_test)
        set_tests_properties(rtmp_monitor_webrtc_loopback_test PROPERTIES
            TIMEOUT 360)

        qt_add_executable(rtmp_monitor_webrtc_endpoint_test
            tests/WebRtcEndpointSessionTest.cpp)
        target_link_libraries(rtmp_monitor_webrtc_endpoint_test PRIVATE
            Qt6::Test
            rtmp_monitor_webrtc_transport
            LibDataChannel::LibDataChannel)
        rtmp_monitor_apply_project_options(rtmp_monitor_webrtc_endpoint_test)
        add_test(NAME rtmp_monitor_webrtc_endpoint_test
            COMMAND rtmp_monitor_webrtc_endpoint_test)
        set_tests_properties(rtmp_monitor_webrtc_endpoint_test PROPERTIES
            RUN_SERIAL TRUE
            TIMEOUT 120)

        qt_add_executable(rtmp_monitor_h264_publisher_source_test
            tests/Mp4H264PublisherSourceTest.cpp)
        target_link_libraries(rtmp_monitor_h264_publisher_source_test PRIVATE
            Qt6::Test
            rtmp_monitor_h264_publisher_source)
        rtmp_monitor_apply_project_options(
            rtmp_monitor_h264_publisher_source_test)
        add_test(NAME rtmp_monitor_h264_publisher_source_test
            COMMAND rtmp_monitor_h264_publisher_source_test)
        set_tests_properties(rtmp_monitor_h264_publisher_source_test PROPERTIES
            RUN_SERIAL TRUE
            TIMEOUT 120)
        rtmp_monitor_copy_ffmpeg_runtime(
            rtmp_monitor_h264_publisher_source_test)

        qt_add_executable(rtmp_monitor_webrtc_publisher_peer
            tests/WebRtcPublisherPeerMain.cpp)
        target_link_libraries(rtmp_monitor_webrtc_publisher_peer PRIVATE
            Qt6::Core
            rtmp_monitor_webrtc_signaling
            rtmp_monitor_webrtc_contracts
            LibDataChannel::LibDataChannel)
        rtmp_monitor_apply_project_options(
            rtmp_monitor_webrtc_publisher_peer)

        foreach(webrtc_test_target IN ITEMS
            rtmp_monitor_webrtc_session_test
            rtmp_monitor_webrtc_h264_test
            rtmp_monitor_webrtc_loopback_test
            rtmp_monitor_webrtc_endpoint_test
            rtmp_monitor_h264_publisher_source_test
            rtmp_monitor_webrtc_publisher_peer)
            set_target_properties(${webrtc_test_target} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY
                    "${CMAKE_BINARY_DIR}/webrtc/$<CONFIG>")
            rtmp_monitor_copy_webrtc_runtime(${webrtc_test_target})
        endforeach()
    else()
        add_test(NAME rtmp_monitor_webrtc_disabled_test
            COMMAND ${CMAKE_COMMAND}
                -DARTIFACT_ROOT=$<TARGET_FILE_DIR:rtmp_monitor>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/VerifyWebRtcDisabled.cmake)
    endif()
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME rtmp_monitor_audio_latency_report_test
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_latency_report_test.py
        )
    endif()

    add_test(
        NAME rtmp_monitor_layer_dependency_test
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckLayerDependencies.cmake
    )

    qt_add_executable(rtmp_monitor_camera_validation_core_test
        tests/CameraValidationCoreTest.cpp
    )
    if(WIN32)
        set_target_properties(rtmp_monitor_camera_validation_core_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    target_link_libraries(rtmp_monitor_camera_validation_core_test
        PRIVATE rtmp_monitor_render rtmp_monitor_ffmpeg Qt6::Test)
    rtmp_monitor_apply_project_options(rtmp_monitor_camera_validation_core_test)
    add_test(NAME rtmp_monitor_camera_validation_core_test
        COMMAND rtmp_monitor_camera_validation_core_test)
    rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_camera_validation_core_test)

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

            qt_add_executable(rtmp_monitor_opengl_grid_renderer_smoke
                tests/OpenGLGridRendererSmoke.cpp
            )
            set_target_properties(rtmp_monitor_opengl_grid_renderer_smoke
                PROPERTIES WIN32_EXECUTABLE FALSE)
            target_link_libraries(rtmp_monitor_opengl_grid_renderer_smoke
                PRIVATE
                    rtmp_monitor_ui
                    Qt6::Test
            )
            rtmp_monitor_apply_project_options(
                rtmp_monitor_opengl_grid_renderer_smoke
            )
            add_test(
                NAME rtmp_monitor_opengl_grid_renderer_smoke
                COMMAND rtmp_monitor_opengl_grid_renderer_smoke
            )
            set_tests_properties(
                rtmp_monitor_opengl_grid_renderer_smoke
                PROPERTIES
                    LABELS "render;opengl"
                    RUN_SERIAL TRUE
                    TIMEOUT 60
            )
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND RTMP_MONITOR_HAS_OPENGL)
            add_executable(rtmp_monitor_opengl_egl_smoke
                tests/OpenGLEglSmoke.cpp
            )
            target_link_libraries(rtmp_monitor_opengl_egl_smoke
                PRIVATE
                    rtmp_monitor_arm64_egl
                    rtmp_monitor_arm64_gles3
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
            rtmp_monitor_diagnostics
            Qt6::Test
    )

    rtmp_monitor_apply_project_options(rtmp_monitor_multi_stream_test)
    add_test(NAME rtmp_monitor_multi_stream_test
        COMMAND rtmp_monitor_multi_stream_test)

    qt_add_executable(rtmp_monitor_audio_playback_test
        tests/AudioPlaybackEngineTest.cpp
    )
    target_include_directories(rtmp_monitor_audio_playback_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/common/media
    )
    target_link_libraries(rtmp_monitor_audio_playback_test PRIVATE
        Qt6::Test
        Qt6::Multimedia
        rtmp_monitor_media
        rtmp_monitor_ffmpeg
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_audio_playback_test)
    add_test(NAME rtmp_monitor_audio_playback_test
        COMMAND rtmp_monitor_audio_playback_test)
    rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_audio_playback_test)
    rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_multi_stream_test)

    if(WIN32)
        qt_add_executable(rtmp_monitor_live_ui_shutdown_test
            tests/LiveUiShutdownTest.cpp
            src/common/app/ConnectionBindingRegistry.cpp
            src/common/app/ConnectionEventReporter.cpp
            include/common/app/StreamConnectionController.h
            src/common/app/StreamConnectionController.cpp
            ${RTMP_MONITOR_STYLE_RESOURCE}
        )
        set_target_properties(rtmp_monitor_live_ui_shutdown_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
        target_include_directories(rtmp_monitor_live_ui_shutdown_test
            PRIVATE
                ${CMAKE_BINARY_DIR}/generated
        )
        target_link_libraries(rtmp_monitor_live_ui_shutdown_test
            PRIVATE
                rtmp_monitor_app
                rtmp_monitor_logging
                rtmp_monitor_media
                rtmp_monitor_server
                rtmp_monitor_ui
                Qt6::Test
        )
        rtmp_monitor_apply_project_options(rtmp_monitor_live_ui_shutdown_test)
        add_test(NAME rtmp_monitor_live_ui_shutdown_test
            COMMAND rtmp_monitor_live_ui_shutdown_test)
        set_tests_properties(rtmp_monitor_live_ui_shutdown_test
            PROPERTIES
                LABELS "windows;live;shutdown"
                RUN_SERIAL TRUE
                TIMEOUT 60
        )
        rtmp_monitor_copy_ffmpeg_runtime(rtmp_monitor_live_ui_shutdown_test)
    endif()

    qt_add_executable(rtmp_monitor_video_render_core_test
        tests/VideoRenderCoreTest.cpp
    )
    if(WIN32)
        set_target_properties(rtmp_monitor_video_render_core_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    target_link_libraries(rtmp_monitor_video_render_core_test
        PRIVATE
            rtmp_monitor_render
            rtmp_monitor_ffmpeg
            Qt6::Test
    )
    target_include_directories(rtmp_monitor_video_render_core_test
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/common/media
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_video_render_core_test)
    add_test(NAME rtmp_monitor_video_render_core_test
        COMMAND rtmp_monitor_video_render_core_test)

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
        src/common/app/ConnectionBindingRegistry.cpp
        src/common/app/ConnectionEventReporter.cpp
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
            rtmp_monitor_server
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

    qt_add_executable(rtmp_monitor_embedded_gl_capabilities_test
        tests/EmbeddedGlCapabilitiesTest.cpp
    )
    if(WIN32)
        set_target_properties(rtmp_monitor_embedded_gl_capabilities_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    target_link_libraries(rtmp_monitor_embedded_gl_capabilities_test
        PRIVATE
            rtmp_monitor_render
            Qt6::Test
    )
    rtmp_monitor_apply_project_options(
        rtmp_monitor_embedded_gl_capabilities_test
    )
    add_test(NAME rtmp_monitor_embedded_gl_capabilities_test
        COMMAND rtmp_monitor_embedded_gl_capabilities_test)

    # Linux 渲染策略为纯判定逻辑，跨平台编译以便在 Windows 上回归。
    qt_add_executable(rtmp_monitor_linux_rendering_policy_test
        tests/LinuxRenderingPolicyTest.cpp
        include/platform/linux/LinuxRenderingPolicy.h
        src/platform/linux/LinuxRenderingPolicy.cpp
    )
    if(WIN32)
        set_target_properties(rtmp_monitor_linux_rendering_policy_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    target_include_directories(rtmp_monitor_linux_rendering_policy_test
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/common
            ${CMAKE_CURRENT_SOURCE_DIR}/include/platform
    )
    target_link_libraries(rtmp_monitor_linux_rendering_policy_test
        PRIVATE
            Qt6::Core
            Qt6::Test
    )
    rtmp_monitor_apply_project_options(
        rtmp_monitor_linux_rendering_policy_test
    )
    add_test(NAME rtmp_monitor_linux_rendering_policy_test
        COMMAND rtmp_monitor_linux_rendering_policy_test)

    qt_add_executable(rtmp_monitor_url_builder_test
        tests/RtmpUrlBuilderTest.cpp
    )
    if(WIN32)
        set_target_properties(rtmp_monitor_url_builder_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    target_link_libraries(rtmp_monitor_url_builder_test
        PRIVATE
            rtmp_monitor_server
            Qt6::Test
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_url_builder_test)
    add_test(NAME rtmp_monitor_url_builder_test
        COMMAND rtmp_monitor_url_builder_test)

    qt_add_executable(rtmp_monitor_media_server_config_test
        tests/MediaServerConfigurationTest.cpp
    )
    if(WIN32)
        set_target_properties(rtmp_monitor_media_server_config_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    target_link_libraries(rtmp_monitor_media_server_config_test
        PRIVATE
            rtmp_monitor_server
            Qt6::Test
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_media_server_config_test)
    add_test(NAME rtmp_monitor_media_server_config_test
        COMMAND rtmp_monitor_media_server_config_test)

    qt_add_executable(rtmp_monitor_media_server_monitor_test
        tests/MediaServerMonitorTest.cpp
    )
    if(WIN32)
        set_target_properties(rtmp_monitor_media_server_monitor_test
            PROPERTIES WIN32_EXECUTABLE FALSE)
    endif()
    target_link_libraries(rtmp_monitor_media_server_monitor_test
        PRIVATE
            rtmp_monitor_server
            Qt6::Test
    )
    rtmp_monitor_apply_project_options(rtmp_monitor_media_server_monitor_test)
    add_test(NAME rtmp_monitor_media_server_monitor_test
        COMMAND rtmp_monitor_media_server_monitor_test)

    # Windows GUI executables normally make QCommandLineParser display the
    # version in a modal dialog.  Keep --version scriptable and guarantee that
    # the process exits without leaving a window behind.
    add_test(NAME rtmp_monitor_version_cli_test
        COMMAND $<TARGET_FILE:rtmp_monitor> --version)
    string(REPLACE "." "[.]" RTMP_MONITOR_VERSION_TEST_REGEX
        "${RTMP_MONITOR_VERSION}")
    set_tests_properties(rtmp_monitor_version_cli_test PROPERTIES
        PASS_REGULAR_EXPRESSION
            "RtmpMonitor ${RTMP_MONITOR_VERSION_TEST_REGEX}"
        TIMEOUT 5
    )
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
    RUNTIME DESTINATION $<IF:$<BOOL:${WIN32}>,.,${CMAKE_INSTALL_BINDIR}>
)

install(FILES resources/styles/app.qss
    DESTINATION $<IF:$<BOOL:${WIN32}>,styles,${CMAKE_INSTALL_BINDIR}/styles>
)

if(WIN32)
    install(FILES
        deploy/srs/media-server.example.ini
        LICENSE
        README_WINDOWS_TEST_PACKAGE.txt
        THIRD_PARTY_NOTICES
        DESTINATION .
    )
endif()

# ---------------------------------------------------------------------------
# 不可含糊的构建配置摘要
# ---------------------------------------------------------------------------
message(STATUS "==================== RtmpMonitor build summary ====================")
message(STATUS "Application version : ${RTMP_MONITOR_VERSION}")
message(STATUS "Target system        : ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Build type           : ${CMAKE_BUILD_TYPE}")
message(STATUS "Linux render mode    : ${RTMP_MONITOR_LINUX_RENDER_MODE_EFFECTIVE}")
message(STATUS "CPU canvas compiled  : ON")
message(STATUS "OpenGL backend       : ${RTMP_MONITOR_HAS_OPENGL}")
message(STATUS "OpenGL prototype     : ${RTMP_MONITOR_BUILD_OPENGL_PROTOTYPE}")
message(STATUS "WebRTC developer path: ${RTMP_MONITOR_ENABLE_WEBRTC}")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Target sysroot       : ${CMAKE_SYSROOT}")
    if(RTMP_MONITOR_HAS_OPENGL)
        message(STATUS "EGL library          : ${RTMP_MONITOR_EGL_LIBRARY}")
        message(STATUS "GLESv2 library       : ${RTMP_MONITOR_GLES3_LIBRARY}")
    else()
        message(STATUS "Qt QPA expectation   : linuxfb / software raster (no EGL/GLES)")
    endif()
endif()
message(STATUS "===================================================================")
