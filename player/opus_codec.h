
#ifndef OPUS_ENCODE_H
#define OPUS_ENCODE_H

#include <opus.h>

#include <inttypes.h>

class OpusEncoder
{
public:
    OpusEncoder(int sample_rate, int channels);
    ~OpusEncoder();
    
    int encode(const int16_t *samles, int sampls_count, uint8_t *encode_frame);

private:
    ::OpusEncoder *handle_;
};

class OpusDecoder
{
public:
    OpusDecoder(int sample_rate, int channels);
    ~OpusDecoder();

    int decode(const uint8_t *frame, int frame_len, int16_t samples[960], int sample_count, int dec_fec);

private:
    ::OpusDecoder *handle_;
};

#endif
