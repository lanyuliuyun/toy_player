cmake_minimum_required(VERSION 3.9)

project(toy_player)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/bin)

include_directories(
    ${LY_ROOT}/include
    ${FFMPEG_ROOT}/include
    ${OPUS_ROOT}/include
)

link_directories(
    ${LY_ROOT}
    ${FFMPEG_ROOT}/lib
    ${OPUS_ROOT}/build
)

if (MSVC)
    add_compile_options(
        /utf-8
        /wd4996
        /DWIN32_LEAN_AND_MEAN
    )
endif()

add_subdirectory(player)
