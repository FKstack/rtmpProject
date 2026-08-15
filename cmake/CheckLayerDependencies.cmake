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

foreach(source_file IN LISTS media_files)
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "#[ \t]*include[ \t]*[<\"](render|ui)/")
        message(FATAL_ERROR "media layer depends on render/ui: ${source_file}")
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
