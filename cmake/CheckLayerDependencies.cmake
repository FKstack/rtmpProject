file(GLOB_RECURSE media_files
    "${PROJECT_SOURCE_DIR}/include/common/media/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/media/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/media/*.cpp")
file(GLOB_RECURSE render_files
    "${PROJECT_SOURCE_DIR}/include/common/render/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/render/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/render/*.cpp")
file(GLOB_RECURSE device_control_files
    "${PROJECT_SOURCE_DIR}/include/common/device_control/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/device_control/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/device_control/*.cpp")
file(GLOB_RECURSE mqtt_transport_files
    "${PROJECT_SOURCE_DIR}/include/common/mqtt_transport/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/mqtt_transport/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/mqtt_transport/*.cpp")
file(GLOB_RECURSE control_policy_files
    "${PROJECT_SOURCE_DIR}/include/common/control_policy/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/control_policy/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/control_policy/*.cpp")
file(GLOB_RECURSE event_center_files
    "${PROJECT_SOURCE_DIR}/include/common/event_center/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/event_center/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/event_center/*.cpp")
file(GLOB_RECURSE evidence_files
    "${PROJECT_SOURCE_DIR}/include/common/evidence/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/evidence/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/evidence/*.cpp")
file(GLOB_RECURSE webrtc_dev_files
    "${PROJECT_SOURCE_DIR}/include/common/webrtc_dev/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/webrtc_dev/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/webrtc_dev/*.cpp")
file(GLOB_RECURSE h264_contract_files
    "${PROJECT_SOURCE_DIR}/include/common/h264/*.h")
file(GLOB_RECURSE webrtc_contract_files
    "${PROJECT_SOURCE_DIR}/include/common/webrtc_contracts/*.h")
file(GLOB_RECURSE webrtc_transport_files
    "${PROJECT_SOURCE_DIR}/include/common/webrtc_transport/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/webrtc_transport/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/webrtc_transport/*.cpp")
file(GLOB_RECURSE webrtc_runtime_files
    "${PROJECT_SOURCE_DIR}/include/common/webrtc_runtime/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/webrtc_runtime/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/webrtc_runtime/*.cpp")
file(GLOB_RECURSE publisher_files
    "${PROJECT_SOURCE_DIR}/include/common/publisher/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/publisher/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/publisher/*.cpp")
file(GLOB_RECURSE identity_contract_files
    "${PROJECT_SOURCE_DIR}/include/common/identity_contracts/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/identity_contracts/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/identity_contracts/*.cpp")
file(GLOB_RECURSE signaling_contract_files
    "${PROJECT_SOURCE_DIR}/include/common/signaling_contracts/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/signaling_contracts/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/signaling_contracts/*.cpp")
file(GLOB_RECURSE signaling_channel_files
    "${PROJECT_SOURCE_DIR}/include/common/signaling_channel/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/signaling_channel/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/signaling_channel/*.cpp")
file(GLOB_RECURSE signaling_session_files
    "${PROJECT_SOURCE_DIR}/include/common/signaling_session/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/signaling_session/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/signaling_session/*.cpp")
file(GLOB_RECURSE mqtt_signaling_files
    "${PROJECT_SOURCE_DIR}/include/common/mqtt_signaling/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/mqtt_signaling/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/mqtt_signaling/*.cpp")
file(GLOB_RECURSE runtime_config_files
    "${PROJECT_SOURCE_DIR}/include/common/runtime_config/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/runtime_config/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/runtime_config/*.cpp")
file(GLOB_RECURSE device_session_files
    "${PROJECT_SOURCE_DIR}/include/common/device_session/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/device_session/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/device_session/*.cpp")
file(GLOB_RECURSE device_agent_session_files
    "${PROJECT_SOURCE_DIR}/include/common/device_agent_session/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/device_agent_session/*.h"
    "${PROJECT_SOURCE_DIR}/src/common/device_agent_session/*.cpp")
set(video_canvas_files
    "${PROJECT_SOURCE_DIR}/include/common/ui/CpuVideoCanvas.h"
    "${PROJECT_SOURCE_DIR}/src/common/ui/CpuVideoCanvas.cpp"
    "${PROJECT_SOURCE_DIR}/include/common/ui/VideoCanvasHost.h"
    "${PROJECT_SOURCE_DIR}/src/common/ui/VideoCanvasHost.cpp"
    "${PROJECT_SOURCE_DIR}/include/common/ui/VideoOpenGLCanvas.h"
    "${PROJECT_SOURCE_DIR}/src/common/ui/VideoOpenGLCanvas.cpp")

foreach(source_file IN LISTS media_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](render|ui)/")
        message(FATAL_ERROR "media layer depends on render/ui: ${source_file}")
    endif()
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"]webrtc_contracts/")
        message(FATAL_ERROR
            "media layer depends on WebRTC session contracts: ${source_file}")
    endif()
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](identity_contracts|signaling_contracts|signaling_channel|signaling_session|mqtt_signaling|runtime_config|device_session|device_agent_session)/")
        message(FATAL_ERROR
            "media layer depends on product signaling/session: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS evidence_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|control_policy|device_control|event_center|logging|media|profiles|render|server|ui)/")
        message(FATAL_ERROR "evidence depends on an outer layer: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS event_center_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|control_policy|device_control|logging|media|profiles|render|server|ui)/")
        message(FATAL_ERROR "event_center depends on an outer layer: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS control_policy_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|device_control|logging|media|render|server|ui)/")
        message(FATAL_ERROR "control_policy depends on an outer layer: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS media_files render_files device_control_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"]control_policy/")
        message(FATAL_ERROR "existing lower layer depends on control_policy: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS media_files render_files device_control_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"]evidence/")
        message(FATAL_ERROR "existing lower layer depends on evidence: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS media_files render_files device_control_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"]event_center/")
        message(FATAL_ERROR "existing lower layer depends on event_center: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS media_files render_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](device_control|profiles)/")
        message(FATAL_ERROR "media/render depends on device_control/profiles: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS device_control_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](ui|media|render)/")
        message(FATAL_ERROR "device_control depends on ui/media/render: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS render_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"]ui/")
        message(FATAL_ERROR "render layer depends on ui: ${source_file}")
    endif()
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](identity_contracts|signaling_contracts|signaling_channel|signaling_session|mqtt_signaling|runtime_config|device_session|device_agent_session)/")
        message(FATAL_ERROR
            "render layer depends on product signaling/session: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS mqtt_transport_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|control_policy|device_control|diagnostics|evidence|event_center|identity_contracts|logging|media|mqtt_signaling|profiles|publisher|render|runtime_config|server|signaling_channel|signaling_contracts|signaling_session|ui|webrtc_dev|webrtc_product|webrtc_runtime|webrtc_transport)/")
        message(FATAL_ERROR
            "MQTT transport depends on a product or outer layer: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS webrtc_dev_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|device_control|diagnostics|evidence|event_center|media|profiles|render|server|ui)/")
        message(FATAL_ERROR
            "Week 2 WebRTC developer boundary depends on a product layer: ${source_file}")
    endif()
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](identity_contracts|signaling_contracts|signaling_channel|signaling_session|mqtt_signaling|runtime_config|device_session|device_agent_session)/")
        message(FATAL_ERROR
            "Week 2 WebRTC developer boundary depends on P2P product signaling/session: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS device_control_files webrtc_transport_files
        webrtc_runtime_files publisher_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](signaling_channel|signaling_session|mqtt_signaling|device_session|device_agent_session)/")
        message(FATAL_ERROR
            "existing lower layer depends on product signaling/session: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS identity_contract_files signaling_contract_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|control_policy|device_control|diagnostics|evidence|event_center|logging|media|mqtt_signaling|profiles|publisher|render|runtime_config|server|signaling_session|ui|webrtc_dev|webrtc_product|webrtc_runtime|webrtc_transport)/")
        message(FATAL_ERROR
            "P2P identity/signaling contract depends on an outer layer: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS signaling_channel_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|device_control|diagnostics|evidence|event_center|logging|media|mqtt_signaling|profiles|publisher|render|server|signaling_session|ui|webrtc_dev|webrtc_product|webrtc_runtime|webrtc_transport)/")
        message(FATAL_ERROR
            "signaling channel abstraction depends on an implementation/product layer: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS mqtt_signaling_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|device_control|diagnostics|evidence|event_center|media|profiles|publisher|render|server|ui|webrtc_dev|webrtc_product|webrtc_runtime|webrtc_transport)/")
        message(FATAL_ERROR
            "MQTT signaling adapter depends on product/media/UI: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS runtime_config_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|device_control|diagnostics|evidence|event_center|media|mqtt_signaling|profiles|publisher|render|server|signaling_session|ui|webrtc_dev|webrtc_product|webrtc_runtime|webrtc_transport)/")
        message(FATAL_ERROR
            "runtime config depends on a product or infrastructure layer: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS h264_contract_files webrtc_contract_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|control_policy|device_control|diagnostics|evidence|event_center|logging|media|profiles|render|server|ui|webrtc_dev)/")
        message(FATAL_ERROR
            "low-level realtime contract depends on an outer layer: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS webrtc_transport_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|device_control|diagnostics|evidence|event_center|logging|media|profiles|publisher|render|server|ui|webrtc_dev)/")
        message(FATAL_ERROR
            "WebRTC transport depends on a source or product layer: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS webrtc_runtime_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|device_control|diagnostics|evidence|event_center|logging|media|profiles|publisher|render|server|ui|webrtc_product)/")
        message(FATAL_ERROR
            "WebRTC runtime depends on a product/media/UI layer: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS publisher_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|device_control|diagnostics|evidence|event_center|logging|media|profiles|render|server|ui|webrtc_contracts|webrtc_dev|webrtc_transport|webrtc_runtime|webrtc_product)/")
        message(FATAL_ERROR
            "publisher source depends on transport or a product layer: ${source_file}")
    endif()
endforeach()

foreach(source_file IN LISTS video_canvas_files)
    if(NOT EXISTS "${source_file}")
        continue()
    endif()
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](app|control_policy|device_control|diagnostics|evidence|event_center|logging|profiles|publisher|server|webrtc_contracts|webrtc_dev|webrtc_transport)/")
        message(FATAL_ERROR
            "reusable video canvas depends on a product/transport layer: ${source_file}")
    endif()
endforeach()
