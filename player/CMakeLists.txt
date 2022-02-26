
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
    PRIVATE TEST_AVC_ENCODE
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

set(ut_audio_cap_source_mf_SRCS
    audio_cap_source_mf.cc
)
add_executable(ut_audio_cap_source_mf ${ut_audio_cap_source_mf_SRCS})
target_compile_definitions(ut_audio_cap_source_mf
    PRIVATE UT_AUDIO_CAP_SOURCE
)
target_link_libraries(ut_audio_cap_source_mf
    Mf.lib
    Mfplat.lib
    Mfuuid.lib
    Mfreadwrite.lib
)

set(ut_video_cap_source_mf_SRCS
    video_cap_source_mf.cc
)
add_executable(ut_video_cap_source_mf ${ut_video_cap_source_mf_SRCS})
target_compile_definitions(ut_video_cap_source_mf
    PRIVATE UT_VIDEO_CAP_SOURCE
)
target_link_libraries(ut_video_cap_source_mf
    Mf.lib
    Mfplat.lib
    Mfuuid.lib
    Mfreadwrite.lib
)

set(ut_audio_cap_source_wi_SRCS
    audio_cap_source_wi.cc
)
add_executable(ut_audio_cap_source_wi ${ut_audio_cap_source_wi_SRCS})
target_compile_definitions(ut_audio_cap_source_wi
    PRIVATE UT_AUDIO_CAP_SOURCE_WI
)
target_link_libraries(ut_audio_cap_source_wi
    Winmm
)

set(ut_audio_cap_source_adv_SRCS
    audio_cap_source_adv.cc
)
add_executable(ut_audio_cap_source_adv ${ut_audio_cap_source_adv_SRCS})
target_compile_definitions(ut_audio_cap_source_adv
    PRIVATE UT_AUDIO_CAP_SOURCE_ADV
)
target_link_libraries(ut_audio_cap_source_adv
    Msdmo
)

set(ut_speaker_cap_source_SRCS
    speaker_cap_source.cc
)
add_executable(ut_speaker_cap_source ${ut_speaker_cap_source_SRCS})
target_compile_definitions(ut_speaker_cap_source
    PRIVATE UT_SPEAKER_CAP_SOURCE
)

set(ut_audio_play_sink_mf_SRCS
    audio_play_sink_mf.cpp
)
add_executable(ut_audio_play_sink_mf ${ut_audio_play_sink_mf_SRCS})
target_compile_definitions(ut_audio_play_sink_mf
    PRIVATE UT_AUDIO_PLAY_SINK_MF
)
target_link_libraries(ut_audio_play_sink_mf
    Mf.lib
    Mfplat.lib
    Mfuuid.lib
    Mfreadwrite.lib
)

set(audio_player_SRCS
    audio_cap_source_mf.cc
    audio_play_sink_mf.cpp
    audio_player.cc
)
add_executable(audio_player ${audio_player_SRCS})
target_link_libraries(audio_player
    Mf.lib
    Mfplat.lib
    Mfuuid.lib
    Mfreadwrite.lib
    avutil.lib
)

set(video_player_SRCS
    avc_decoder_mf.cc
    avc_frame_generator_mf.cc
    frame_allocator.cc
    image_allocator.cc
    video_player.cc
    render_d2d.cc
)
add_executable(video_player ${video_player_SRCS})
target_link_libraries(video_player
    D2d1.lib
    yuv.lib
    Mfplat.lib
    Mfuuid.lib
)
