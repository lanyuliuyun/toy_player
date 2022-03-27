
#include "audio_cap_source_mf.h"
#include "audio_play_sink_mf.h"

extern "C" {
#include <libavutil/audio_fifo.h>
}

#include <Objbase.h>
#include <mfapi.h>
#include <stdio.h>

#include <mutex>

class AudioSampleQueue
{
public:
    AudioSampleQueue() : audio_queue_(NULL), sample_pts_(0), audio_queue_lock_()
    {
        audio_queue_ = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, 1, 1920);
    }
    ~AudioSampleQueue() {
        av_audio_fifo_free(audio_queue_);
    }

    void put(int16_t* sample, int sample_count, int64_t pts)
    {
        std::lock_guard<std::mutex> guard(audio_queue_lock_);
        av_audio_fifo_write(audio_queue_, (void**)&sample, sample_count);
    }

    int get(int16_t* samples, int max_sample_count)
    {
        std::lock_guard<std::mutex> guard(audio_queue_lock_);

        int ret = av_audio_fifo_read(audio_queue_, (void**)&samples, max_sample_count);
        return ret > 0 ? ret : 0;
    }

private:
    AVAudioFifo *audio_queue_;
    LONGLONG sample_pts_;
    std::mutex audio_queue_lock_;
};

int main(int argc, char *argv[])
{
    CoInitializeEx(0, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, 0);
    {
    AudioSampleQueue queue;
    AudioCapSourceMF cap([&queue](int16_t* sample, int sample_count, int64_t pts){ queue.put(sample, sample_count, pts); }, NULL);

    DWORD sample_rate, channels;
    cap.getSpec(&sample_rate, &channels);
    printf("audio cap spec: sample_rate: %u, channels: %u\n", sample_rate, channels);

    AudioPlayMF play([&queue](int16_t* samples, int max_sample_count){ return queue.get(samples, max_sample_count); }, NULL);

    cap.start();
    play.start();

    getchar();
    
    cap.stop();
    play.stop();
    }
    MFShutdown();
    CoUninitialize();

    return 0;
}
