if(NOT DEFINED ARTIFACT_ROOT OR ARTIFACT_ROOT STREQUAL "")
    message(FATAL_ERROR "The OFF artifact root is required.")
endif()

foreach(forbidden_name IN ITEMS
    datachannel.dll
    juice.dll
    srtp2.dll
    libssl-3-x64.dll
    libcrypto-3-x64.dll
    rtmp_monitor_webrtc_probe.exe
    rtmp_monitor_webrtc_client.exe
    rtmp_monitor_webrtc_stun_fixture.exe
    rtmp_monitor_webrtc_publisher_peer.exe)
    if(EXISTS "${ARTIFACT_ROOT}/${forbidden_name}")
        message(FATAL_ERROR
            "The OFF build contains a forbidden optional transport artifact: ${forbidden_name}")
    endif()
endforeach()

message(STATUS "The OFF build contains no optional transport runtime artifacts.")
