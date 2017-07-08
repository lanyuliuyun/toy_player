
#ifndef ENCODE_H
#define ENCODE_H

#include <stdint.h>

typedef struct i420_image
{
    int width;
    int height;
    void* data;
    void* y_ptr;
    void* u_ptr;
    void* v_ptr;
}i420_image_t;

typedef struct avc_encoder avc_encoder_t;

#ifdef __cplusplus
extern "C" {
#endif

i420_image_t* i420_image_alloc(int width, int height);

void i420_image_free(i420_image_t* image);

void fill_image_i420(i420_image_t* image, int index);

avc_encoder_t* avc_encoder_new
(
    int width, int height, int fps, int bitrate,
    void (*on_nalu_data)(void* nalu, unsigned len, int is_last, void* userdata), void *userdata
);

int avc_encoder_init(avc_encoder_t* encoder);

int avc_encoder_encode(avc_encoder_t* encoder, i420_image_t* image, int pts, int keyframe);

void avc_encoder_destroy(avc_encoder_t* encoder);

#ifdef __cplusplus
}
#endif

#endif
