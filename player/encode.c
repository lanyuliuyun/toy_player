
#include "encode.h"

#include <x264.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct avc_encoder
{
    x264_param_t x264_param;
    x264_t* x264;

    x264_picture_t in_image;
    x264_picture_t out_frame;

    int width;
    int height;
    int bitrate;
    int fps;

    int gop_size;
    int gop_frame_count;

    void (*on_nalu_data)(void* nalu, unsigned len, int is_last, int is_key, void* userdata);
    void *userdata;
};

avc_encoder_t* avc_encoder_new
(
    int width, int height, int fps, int bitrate,
    void (*on_nalu_data)(void* frame, unsigned len, int is_last,  int is_key, void* userdata), void *userdata
)
{
    avc_encoder_t* encoder;

    encoder = malloc(sizeof(*encoder));
    memset(encoder, 0, sizeof(*encoder));
    encoder->width = width;
    encoder->height = height;
    encoder->fps = fps;
    encoder->bitrate = bitrate;

    encoder->on_nalu_data = on_nalu_data;
    encoder->userdata = userdata;

    encoder->gop_size = 25;
    encoder->gop_frame_count = 0;

    return encoder;
}

int avc_encoder_init(avc_encoder_t* encoder)
{
    int ret;

    x264_param_default(&encoder->x264_param);

    //x264_param_default_preset(&encoder->x264_param, "veryfast", "animation");

    encoder->x264_param.i_width = encoder->width;
    encoder->x264_param.i_height = encoder->height;
    encoder->x264_param.i_csp = X264_CSP_I420;

    encoder->x264_param.rc.i_rc_method = X264_RC_ABR;

    /* QP 确定画面细节保留程度，取值越小，细节越丰富 */
    encoder->x264_param.rc.i_qp_min = 18;
    encoder->x264_param.rc.i_qp_max = 30;
    encoder->x264_param.rc.i_qp_step = 3;

    encoder->x264_param.rc.i_bitrate = encoder->bitrate;

    encoder->x264_param.b_repeat_headers = 1;
    encoder->x264_param.b_annexb = 1;

    encoder->x264_param.i_fps_num = encoder->fps;
    encoder->x264_param.i_fps_den = 1;

    encoder->x264_param.i_timebase_num  = 1;
    encoder->x264_param.i_timebase_den  = encoder->fps;

    encoder->x264_param.i_frame_reference = 1;
    encoder->x264_param.i_dpb_size = 1;
    encoder->x264_param.b_intra_refresh = 0; /*  need i_frame_reference and  i_dpb_size to be both 1 to enable intra_refresh option */

    encoder->x264_param.i_keyint_max = encoder->fps;

    encoder->x264_param.i_bframe = 0;

    encoder->x264_param.b_deblocking_filter = 1;

    /* 设置 NALU 尺寸 */
    /* encoder->x264_param.i_slice_max_size = 1024; */

    /* more options */

    ret = x264_param_apply_profile(&encoder->x264_param, "high");
    if (ret != 0)
    {
        printf("x264_encoder_init, x264_param_apply_profile() failed, ret: 0x%X\n", ret);
        return -1;
    }

    encoder->x264 = x264_encoder_open(&encoder->x264_param);
    if (NULL == encoder->x264)
    {
        printf("x264_encoder_init, x264_encoder_open() failed\n");
        return -1;
    }

    return 0;
}

int avc_encoder_encode(avc_encoder_t* encoder, i420_image_t* image, int pts, int keyframe)
{
    int ret;
    int i;

    x264_nal_t *p_nals;
    int nalu_count;
    int is_key;

    if (NULL != image)
    {
        encoder->gop_frame_count++;
        x264_picture_init(&encoder->in_image);

        if (keyframe)
        {
            encoder->in_image.i_type = X264_TYPE_IDR;
        }
        else
        {
            encoder->in_image.i_type = X264_TYPE_AUTO;
          #if 0
            if (encoder->gop_frame_count >= encoder->gop_size)
            {
                encoder->gop_frame_count = 0;
                x264_encoder_intra_refresh(encoder->x264);
            }
          #endif
        }

        encoder->in_image.i_pts = pts;

        encoder->in_image.img.i_csp = X264_CSP_I420;
        encoder->in_image.img.i_plane = 3;
        encoder->in_image.img.i_stride[0] = image->y_stride;
        encoder->in_image.img.plane[0] = image->y_ptr;
        encoder->in_image.img.i_stride[1] = image->u_stride;
        encoder->in_image.img.plane[1] = image->u_ptr;
        encoder->in_image.img.i_stride[2] = image->v_stride;
        encoder->in_image.img.plane[2] = image->v_ptr;

        p_nals = NULL;
        nalu_count = 0;
        ret = x264_encoder_encode(encoder->x264, &p_nals, &nalu_count, &encoder->in_image, &encoder->out_frame);
        if (ret < 0)
        {
            printf("avc_encoder_encode, x264_encoder_encode() failed, ret: 0x%X\n", ret);
            return -1;
        }

        is_key = IS_X264_TYPE_I(encoder->out_frame->i_type);
        for (i = 0; i < nalu_count; ++i)
        {
            encoder->on_nalu_data(p_nals[i].p_payload, (p_nals[i].i_payload), ((i+1) == nalu_count), is_key, encoder->userdata);
        }
    }
    else
    {
        while (x264_encoder_delayed_frames(encoder->x264))
        {
            p_nals = NULL;
            nalu_count = 0;
            ret = x264_encoder_encode(encoder->x264, &p_nals, &nalu_count, NULL, &encoder->out_frame);
            if (ret < 0)
            {
                printf("avc_encoder_encode, delay, x264_encoder_encode() failed\n");
                break;
            }

            is_key = IS_X264_TYPE_I(encoder->out_frame->i_type);
            for (i = 0; i < nalu_count; ++i)
            {
                encoder->on_nalu_data(p_nals[i].p_payload, (p_nals[i].i_payload), ((i+1) == nalu_count), is_key, encoder->userdata);
            }
        }
    }

    return 0;
}

void avc_encoder_destroy(avc_encoder_t* encoder)
{
    if (NULL != encoder->x264)
    {
        x264_encoder_close(encoder->x264);
    }
    free(encoder);

    return;
}

/********************************************************************************/

#ifdef TEST_ENCODE

void on_nalu_data(void* nalu, unsigned len, int is_last, int is_key, void* userdata)
{
    FILE* fp = (FILE*)userdata;
    fwrite(nalu, 1, len, fp);

    return;
}

int main(int argc, char *argv[])
{
    int width = 640;
    int height = 480;
    int count = 100;
    int fps = 30;
    int bitrate = 128;
    const char* out_file = "x264.h264";

    FILE* fp;

    avc_encoder_t* encoder;
    i420_image_t *image;
    int i;

    uint64_t ts1, ts2;

    printf("x264_bit_depth: %d\n", x264_bit_depth);

    if (argc > 3)
    {
        width = atoi(argv[1]);
        height = atoi(argv[2]);
    }
    if (argc > 4)
    {
        count = atoi(argv[3]);
    }
    if (argc > 5)
    {
        fps = atoi(argv[4]);
    }
    if (argc > 6)
    {
        bitrate = atoi(argv[5]);
    }

    fp = fopen(out_file, "wb");

    encoder = avc_encoder_new(width, height, fps, bitrate, on_nalu_data, fp);

    avc_encoder_init(encoder);

    image = i420_image_alloc(width, height);

    ts1 = now();
    for (i = 0; i < count; ++i)
    {
        fill_image_i420(image, i);
        /* avc_encoder_encode(encoder, image, i, ((i%30) == 0)); */
        avc_encoder_encode(encoder, image, i, 0);
    }

    avc_encoder_encode(encoder, NULL, 0, 0);

    ts2 = now();

    printf("%d frame encoded, time elapsed: %"PRIu64"us, fps: %.3f\n", count, (ts2-ts1), (count / (1.0 * (ts2-ts1) / 1000000)));

    i420_image_free(image);
    avc_encoder_destroy(encoder);

    fclose(fp);

    return 0;
}

#endif // !TEST_ENCODE
