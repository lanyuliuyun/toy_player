
#include "opus_codec.h"

OpusEncoder::OpusEncoder(int sample_rate, int channels) : handle_(NULL)
{
    int err;
    handle_ = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_AUDIO, &err);
    
    opus_encoder_ctl(handle_, OPUS_SET_COMPLEXITY(8));
    opus_encoder_ctl(handle_, OPUS_SET_BITRATE(64000));
    opus_encoder_ctl(handle_, OPUS_SET_VBR(1));
    //opus_encoder_ctl(handle_, OPUS_SET_VBR_CONSTRAINT(1));
    opus_encoder_ctl(handle_, OPUS_SET_FORCE_CHANNELS(1));
    //opus_encoder_ctl(handle_, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
    opus_encoder_ctl(handle_, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
    //opus_encoder_ctl(handle_, OPUS_SET_SIGNAL(OPUS_AUTO));
    //opus_encoder_ctl(handle_, OPUS_SET_INBAND_FEC(0));
    //opus_encoder_ctl(handle_, OPUS_SET_PACKET_LOSS_PERC(40));
    //opus_encoder_ctl(handle_, OPUS_SET_DTX(0));
    opus_encoder_ctl(handle_, OPUS_SET_LSB_DEPTH(16));
    opus_encoder_ctl(handle_, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_20_MS));
    opus_encoder_ctl(handle_, OPUS_SET_PREDICTION_DISABLED(0));
    
    return;
}

OpusEncoder::~OpusEncoder()
{
    if (handle_) { opus_encoder_destroy(handle_); }

    return;
}

int OpusEncoder::encode(const int16_t *samles, int sampls_count, uint8_t encode_frame[960])
{
    if (handle_) { return -1; }
    
    int ret = opus_encode(handle_, samles, sampls_count, encode_frame, 960);

    return ret;
}

OpusDecoder::OpusDecoder(int sample_rate, int channels) : handle_(NULL)
{
    int err;
    handle_ = opus_decoder_create(sample_rate, channels, &err);
    
    return;
}

OpusDecoder::~OpusDecoder()
{
    if (handle_) { opus_decoder_destroy(handle_); }
    return;
}

int OpusDecoder::decode(const uint8_t *frame, int frame_len, int16_t samples[960], int sample_count, int dec_fec)
{
    if (handle_ == NULL) { return -1; }

    int ret = opus_decode(handle_, frame, frame_len, samples, sample_count, dec_fec);

    return ret;
}
