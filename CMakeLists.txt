cmake_minimum_required(VERSION 3.9)

project(toy_player)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/bin)

set(LY_ROOT F:/opensource/libyuv)
set(FFMPEG_ROOT D:/ffmpeg-n4.4-178-g4b583e5425-win64-gpl-shared-4.4)

include_directories(
    ${LY_ROOT}/include
    ${FFMPEG_ROOT}/include
)

link_directories(
    ${LY_ROOT}
    ${FFMPEG_ROOT}/lib
)

if (MSVC)
    add_compile_options(
        /utf-8
        /wd4996
    )
endif()

add_subdirectory(player)
