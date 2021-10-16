
set(ut_d2d_render_SRCS
    image_allocator.cc
    render_d2d.cc
)
add_executable(ut_d2d_render ${ut_d2d_render_SRCS})
target_compile_definitions(ut_d2d_render
    PRIVATE UT_D2D_RENDER
)
target_link_libraries(ut_d2d_render
    D2d1.lib
    yuv.lib
)

set(ut_avc_frame_generator_SRCS
    avc_frame_generator_mf.cc
    frame_allocator.cc
    image_allocator.cc
)
add_executable(ut_avc_frame_generator ${ut_avc_frame_generator_SRCS})
target_compile_definitions(ut_avc_frame_generator
    PRIVATE AVC_ENCODE_TEST
)
target_link_libraries(ut_avc_frame_generator
    yuv.lib
    Mfplat.lib
    Mfuuid.lib
)

set(ut_avc_decoder_SRCS
    avc_decoder_mf.cc
    frame_allocator.cc
    image_allocator.cc
)
add_executable(ut_avc_decoder ${ut_avc_decoder_SRCS})
target_compile_definitions(ut_avc_decoder
    PRIVATE TEST_AVC_DECODER
)
target_link_libraries(ut_avc_decoder
    Mfplat.lib
    Mfuuid.lib
)

set(toy_player_SRCS
    avc_decoder_mf.cc
    avc_frame_generator_mf.cc
    frame_allocator.cc
    image_allocator.cc
    player.cc
    render_d2d.cc
)
add_executable(toy_player ${toy_player_SRCS})
target_link_libraries(toy_player
    D2d1.lib
    yuv.lib
    Mfplat.lib
    Mfuuid.lib
)
