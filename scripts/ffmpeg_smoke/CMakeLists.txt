cmake_minimum_required(VERSION 3.21)

project(RtmpMonitorFFmpegEnvironmentSmoke LANGUAGES C)

find_package(FFMPEG REQUIRED)

add_executable(ffmpeg_environment_smoke ffmpeg_environment_smoke.c)
target_include_directories(ffmpeg_environment_smoke PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(ffmpeg_environment_smoke PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(ffmpeg_environment_smoke PRIVATE ${FFMPEG_LIBRARIES})
