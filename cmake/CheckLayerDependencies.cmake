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

foreach(source_file IN LISTS media_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](render|ui)/")
        message(FATAL_ERROR "media layer depends on render/ui: ${source_file}")
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
endforeach()
