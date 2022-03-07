
#include "audio_cap_source_mf.h"
#include "opus_codec.h"
#include "audio_play_sink_mf.h"

extern "C" {
#include <libavutil/audio_fifo.h>
}

#include <Objbase.h>
#include <mfapi.h>

#include <inttypes.h>
#include <functional>
#include <mutex>

class AudioSampleQueueCap
{
public:
    typedef std::function<void(const int16_t* sample, int sample_count, int64_t pts)> AudioFrameSink;

    AudioSampleQueueCap(int out_frame_samle_count, const AudioFrameSink &sink)
        : audio_queue_(NULL)
        , earlies_pts_(-1)
        , audio_queue_lock_()
        , out_frame_samle_count_(out_frame_samle_count)
        , sink_(sink)
    {
        audio_queue_ = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, 1, 1920);
        audio_frame_ = new int16_t[out_frame_samle_count_];
    }
    ~AudioSampleQueueCap() {
        av_audio_fifo_free(audio_queue_);
        delete[] audio_frame_;
    }

    void put(int16_t* sample, int sample_count, int64_t pts)
    {
        std::lock_guard<std::mutex> guard(audio_queue_lock_);
        av_audio_fifo_write(audio_queue_, (void**)&sample, sample_count);
        if (earlies_pts_ < 0) { earlies_pts_ = pts; }

        if (av_audio_fifo_size(audio_queue_) >= out_frame_samle_count_)
        {
            av_audio_fifo_read(audio_queue_, (void**)&audio_frame_, out_frame_samle_count_);
            sink_(audio_frame_, out_frame_samle_count_, earlies_pts_);
            earlies_pts_ += 20;
        }
    }

private:
    AVAudioFifo *audio_queue_;
    std::mutex audio_queue_lock_;

    int out_frame_samle_count_;
    int16_t *audio_frame_;
    int64_t earlies_pts_;

    AudioFrameSink sink_;
};

class AudioSampleQueuePlay
{
public:
    AudioSampleQueuePlay() : audio_queue_(NULL), sample_pts_(0), audio_queue_lock_()
    {
        audio_queue_ = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, 1, 1920);
    }
    ~AudioSampleQueuePlay() {
        av_audio_fifo_free(audio_queue_);
    }

    void put(int16_t* sample, int sample_count, int64_t pts)
    {
        std::lock_guard<std::mutex> guard(audio_queue_lock_);
        av_audio_fifo_write(audio_queue_, (void**)&sample, sample_count);
    }

    IMFSample* get()
    {
        std::lock_guard<std::mutex> guard(audio_queue_lock_);

        int sample_count = av_audio_fifo_size(audio_queue_);

        enum { kAudioFrameSampleCount = 960 };

        IMFSample *sample = NULL;
        MFCreateSample(&sample);

        IMFMediaBuffer *buf = NULL;
        MFCreateMemoryBuffer((kAudioFrameSampleCount * 2), &buf);
        BYTE *data = NULL;
        buf->Lock(&data, NULL, NULL);
        if (sample_count > kAudioFrameSampleCount)
        {
            av_audio_fifo_read(audio_queue_, (void**)&data, kAudioFrameSampleCount);
        }
        else
        {
            memset(data, 0, (kAudioFrameSampleCount * 2));
        }
        buf->Unlock();
        buf->SetCurrentLength((kAudioFrameSampleCount * 2));

        sample->AddBuffer(buf);
        buf->Release();

        LONGLONG duration = 10000 * 20;
        sample->SetSampleDuration(duration);
        sample->SetSampleFlags(0);
        sample->SetSampleTime(sample_pts_);

        sample_pts_ += duration;

        return sample;
    }

private:
    AVAudioFifo *audio_queue_;
    LONGLONG sample_pts_;
    std::mutex audio_queue_lock_;
};

class AudioCodec
{
public:
    typedef std::function<void(uint8_t* data, int size, int64_t pts)> EncFrameSink;
    typedef std::function<void(int16_t* sample, int sample_count, int64_t pts)> AudioFrameSink;

    AudioCodec(int sample_rate, int channels, const EncFrameSink &enc_sink, const AudioFrameSink &dec_sink)
        : frame_samples_count_((sample_rate * channels) / 50)

        , enc_(sample_rate, channels)
        , enc_sink_(enc_sink)

        , dec_(sample_rate, channels)
        , dec_sink_(dec_sink)

        , audio_frame_output_(new uint8_t[frame_samples_count_])
        , pcm_frame_output_(new int16_t[frame_samples_count_])
    { }

    ~AudioCodec() { delete[] audio_frame_output_; }

    void encode(const int16_t* sample, int sample_count, int64_t pts)
    {
        int ret = enc_.encode(sample, sample_count, audio_frame_output_);
        if (ret > 0)
        {
            enc_sink_(audio_frame_output_, ret, pts);
        }

        return;
    }

    int decode(const uint8_t *frame, int frame_len, int pts)
    {
        int ret = dec_.decode(frame, frame_len, pcm_frame_output_, frame_samples_count_, 0);
        if (ret > 0)
        {
            dec_sink_(pcm_frame_output_, ret, pts);
        }

        return 0;
    }

private:
    int frame_samples_count_;

    OpusEncoder enc_;
    EncFrameSink enc_sink_;

    OpusDecoder dec_;
    AudioFrameSink dec_sink_;

    uint8_t *audio_frame_output_;
    int16_t *pcm_frame_output_;
};

class AudioFilter
{
public:
    typedef std::function<void(const uint8_t* data, int size, int64_t pts)> EncFrameSink;
    typedef std::function<void(int16_t* sample, int sample_count, int64_t pts)> AudioFrameSink;

    AudioFilter(): enc_frame_handler_() , pcm_frame_handler_()
    { }

    void handle_enc_frame(uint8_t* data, int size, int64_t pts)
    {
        enc_frame_handler_(data, size, pts);
        return;
    }

    void handle_pcm_frame(int16_t* sample, int sample_count, int64_t pts)
    {
        pcm_frame_handler_(sample, sample_count, pts);
        return;
    }

    void SetEncFrameSink(const EncFrameSink &sink) { enc_frame_handler_ = sink; }
    void SetPCMFrameSink(const AudioFrameSink &sink) { pcm_frame_handler_ = sink; }
private:
    EncFrameSink enc_frame_handler_;
    AudioFrameSink pcm_frame_handler_;
};

void audio_main(int argc, char *argv[])
{
    AudioFilter filter;

    AudioSampleQueuePlay play_queue;
    AudioPlayMF play([&play_queue]() -> IMFSample*{ return play_queue.get(); });

    AudioCodec codec(48000, 1, [&filter](uint8_t* data, int size, int64_t pts){
        filter.handle_enc_frame(data, size, pts);
    },[&filter](int16_t* sample, int sample_count, int64_t pts){
        filter.handle_pcm_frame(sample, sample_count, pts);
    });

    AudioSampleQueueCap cap_queue(960, [&codec](const int16_t* sample, int sample_count, int64_t pts){
        codec.encode(sample, sample_count, pts);
    });

    AudioCapSourceMF cap([&cap_queue](int16_t* sample, int sample_count, int64_t pts){
        cap_queue.put(sample, sample_count, pts);
    }, NULL);

    filter.SetEncFrameSink([&codec](const uint8_t* data, int size, int64_t pts){
        codec.decode(data, size, pts);
    });
    filter.SetPCMFrameSink([&play_queue](int16_t* sample, int sample_count, int64_t pts){
        play_queue.put(sample, sample_count, pts);
    });

    play.start();
    cap.start();

    getchar();

    cap.stop();
    play.stop();

    return;
}

int main(int argc, char *argv[])
{
    CoInitializeEx(0, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, 0);

    audio_main(argc, argv);

    MFShutdown();
    CoUninitialize();

    return 0;
}
