include_guard(GLOBAL)

#[=======================================================================[.rst:
rtmp_monitor_apply_project_options
----------------------------------

为项目目标统一启用 C++17 和当前编译器对应的警告、语言一致性及源码编码选项。
该函数只设置编译选项，不负责选择目标平台或第三方依赖。
#]=======================================================================]
function(rtmp_monitor_apply_project_options target_name)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "Unknown CMake target: ${target_name}")
    endif()

    target_compile_features(${target_name} PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(${target_name}
            PRIVATE
                /W4
                /Zc:__cplusplus
                /utf-8
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        target_compile_options(${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )
    else()
        message(FATAL_ERROR
            "No compiler options are defined for ${CMAKE_CXX_COMPILER_ID}."
        )
    endif()
endfunction()
