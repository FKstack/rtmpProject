cmake_minimum_required(VERSION 3.21)

project(RtmpMonitor
    VERSION 0.1.0
    DESCRIPTION "PC multi-stream RTMP monitor"
    LANGUAGES CXX
)

if(NOT MSVC)
    message(FATAL_ERROR "RtmpMonitor must be configured with an MSVC compiler.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)

if(QT_QMAKE_EXECUTABLE MATCHES "[/\\\\]mingw")
    message(FATAL_ERROR
        "Detected a MinGW Qt kit at: ${QT_QMAKE_EXECUTABLE}\n"
        "RtmpMonitor requires an MSVC Qt 6 kit. Configure with CMAKE_PREFIX_PATH "
        "pointing to a kit such as D:/Qt/6.x.x/msvc2022_64."
    )
endif()

qt_standard_project_setup()

set(RTMP_MONITOR_APP_SOURCES
    include/core/Singleton.h
    include/app/StyleLoader.h
    src/app/StyleLoader.cpp
)

set(RTMP_MONITOR_STYLE_RESOURCE
    resources/styles.qrc
)

qt_add_library(rtmp_monitor_app STATIC
    ${RTMP_MONITOR_APP_SOURCES}
)

target_include_directories(rtmp_monitor_app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rtmp_monitor_app
    PUBLIC
        Qt6::Widgets
)

target_compile_options(rtmp_monitor_app
    PRIVATE
        /Zc:__cplusplus
        /utf-8
)

# 将可复用的 UI 组合为独立静态库，使图形程序和冒烟测试共享同一份实现。
set(RTMP_MONITOR_UI_SOURCES
    include/ui/FullscreenControlBar.h
    src/ui/FullscreenControlBar.cpp
    include/ui/FullscreenVideoWindow.h
    src/ui/FullscreenVideoWindow.cpp
    include/ui/VideoWidget.h
    src/ui/VideoWidget.cpp
    include/ui/VideoGridWidget.h
    src/ui/VideoGridWidget.cpp
)

qt_add_library(rtmp_monitor_ui STATIC
    ${RTMP_MONITOR_UI_SOURCES}
)

target_include_directories(rtmp_monitor_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rtmp_monitor_ui
    PUBLIC
        Qt6::Widgets
)

target_compile_options(rtmp_monitor_ui
    PRIVATE
        /Zc:__cplusplus
        /utf-8
)

qt_add_executable(rtmp_monitor
    WIN32
    src/main.cpp
    include/ui/MainWindow.h
    src/ui/MainWindow.cpp
    ${RTMP_MONITOR_STYLE_RESOURCE}
)

target_link_libraries(rtmp_monitor
    PRIVATE
        rtmp_monitor_app
        rtmp_monitor_ui
)

target_compile_options(rtmp_monitor
    PRIVATE
        /Zc:__cplusplus
        /utf-8
)

include(CTest)

if(BUILD_TESTING)
    # 冒烟测试只链接 UI 库，避免测试目标意外依赖后续播放器或网络模块。
    qt_add_executable(rtmp_monitor_ui_smoke_test
        tests/VideoGridSmokeTest.cpp
        ${RTMP_MONITOR_STYLE_RESOURCE}
    )

    # 测试目标使用 Console 子系统，使 CTest 能捕获 qCritical() 的失败诊断。
    set_target_properties(rtmp_monitor_ui_smoke_test PROPERTIES WIN32_EXECUTABLE FALSE)

    target_link_libraries(rtmp_monitor_ui_smoke_test
        PRIVATE
            rtmp_monitor_app
            rtmp_monitor_ui
    )

    target_compile_options(rtmp_monitor_ui_smoke_test
        PRIVATE
            /Zc:__cplusplus
            /utf-8
    )

    add_test(NAME rtmp_monitor_ui_smoke_test COMMAND rtmp_monitor_ui_smoke_test)
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
