cmake_minimum_required(VERSION 3.21)

project(WebRtcMiniLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(LibDataChannel 0.24.5 EXACT CONFIG QUIET)
if(NOT LibDataChannel_FOUND)
    message(FATAL_ERROR
        "WebRtcMiniLab requires libdatachannel 0.24.5. "
        "Install that exact version for the target architecture and reconfigure."
    )
endif()
if(NOT TARGET LibDataChannel::LibDataChannel)
    message(FATAL_ERROR
        "libdatachannel 0.24.5 was found without the required "
        "LibDataChannel::LibDataChannel imported target."
    )
endif()

add_executable(webrtc_minilab main.cpp)
target_link_libraries(webrtc_minilab PRIVATE LibDataChannel::LibDataChannel)

if(MSVC)
    target_compile_options(webrtc_minilab PRIVATE /W4 /permissive-)
else()
    target_compile_options(webrtc_minilab PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_custom_command(TARGET webrtc_minilab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:webrtc_minilab>
            $<TARGET_FILE_DIR:webrtc_minilab>
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME webrtc_minilab_single COMMAND webrtc_minilab)
    add_test(NAME webrtc_minilab_repeat_10
        COMMAND webrtc_minilab --repeat=10)
    set_tests_properties(
        webrtc_minilab_single
        webrtc_minilab_repeat_10
        PROPERTIES
            RUN_SERIAL TRUE
            TIMEOUT 120
    )
    set_tests_properties(webrtc_minilab_single PROPERTIES
        PASS_REGULAR_EXPRESSION "summary rounds=1 result=success")
    set_tests_properties(webrtc_minilab_repeat_10 PROPERTIES
        PASS_REGULAR_EXPRESSION "summary rounds=10 result=success")
endif()
