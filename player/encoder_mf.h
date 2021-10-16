
#ifndef ENCODER_MF_H
#define ENCODER_MF_H

#include "image_allocator.h"

#include <stdint.h>

typedef struct avc_encoder avc_encoder_t;

#ifdef __cplusplus
extern "C" {
#endif

avc_encoder_t* avc_encoder_new
(
    int width, int height, int fps, int bitrate,
    void (*on_nalu_data)(void* nalu, unsigned len, int is_last, int is_key,void* userdata), void *userdata
);

int avc_encoder_init(avc_encoder_t* encoder);

int avc_encoder_encode(avc_encoder_t* encoder, i420_image_t* image, int pts, int keyframe);

void avc_encoder_destroy(avc_encoder_t* encoder);

#ifdef __cplusplus
}
#endif

#endif // !ENCODER_MF_H
