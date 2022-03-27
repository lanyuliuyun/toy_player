
#ifndef OPUS_ENCODER_MF_H
#define OPUS_ENCODER_MF_H

#include <mftransform.h>

#include <stdint.h>

class AACEncoder
{
public:
    AACEncoder(int sample_bits, int sample_rate, int channels);
    ~AACEncoder();

    int put(int16_t *samples, int samples_count, int64_t pts);
    int get(uint8_t **data);

private:
    void setup(int sample_bits, int sample_rate, int channels);

	IMFSample* alloc_sample(int size);
	void free_sample(IMFSample* sample);

private:
    IMFTransform* audio_encoder_;

	DWORD input_stream_id_;

	DWORD output_stream_id_;
	/* 是否有MFT来分配buffer，而不是client分配 */
	bool need_client_allocate_output_buffer_;
    DWORD min_output_buffer_size_;

    uint8_t *output_buffer_;
};

#endif // OPUS_ENCODER_MF_H
