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

set(RTMP_MONITOR_UI_SOURCES
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
)

target_link_libraries(rtmp_monitor
    PRIVATE
        rtmp_monitor_ui
)

target_compile_options(rtmp_monitor
    PRIVATE
        /Zc:__cplusplus
        /utf-8
)

include(CTest)

if(BUILD_TESTING)
    qt_add_executable(rtmp_monitor_ui_smoke_test
        tests/VideoGridSmokeTest.cpp
    )

    target_link_libraries(rtmp_monitor_ui_smoke_test
        PRIVATE
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

install(TARGETS rtmp_monitor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
