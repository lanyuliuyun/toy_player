
#include "audio_cap_source_mf.h"
#include "audio_play_mf.h"

extern "C" {
#include <libavutil/audio_fifo.h>
}

#include <Objbase.h>
#include <mfapi.h>

#include <list>
#include <mutex>
#include <functional>

class AudioSampleQueue
{
public:
    AudioSampleQueue()
    {
        audio_queue_ = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, 1, 1920);
        sample_pts_ = 0;
    }
    ~AudioSampleQueue() {
        av_audio_fifo_free(audio_queue_);
    }

    void put(IMFSample* sample)
    {
        std::lock_guard<std::mutex> guard(audio_queue_lock_);

        DWORD buf_count = 0;
        sample->GetBufferCount(&buf_count);
        for (DWORD i = 0; i < buf_count; ++i)
        {
            IMFMediaBuffer *buf = NULL;
            sample->GetBufferByIndex(i, &buf);
            if (buf)
            {
                BYTE *data = NULL;
                DWORD data_len = 0;
                buf->Lock(&data, NULL, &data_len);
                av_audio_fifo_write(audio_queue_, (void**)&data, (data_len>>1));
                buf->Unlock();
                buf->Release();
            }
        }
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
    LONGLONG sample_pts_ = 0;
    std::mutex audio_queue_lock_;
};

int wmain(int argc, wchar_t *argv[])
{
    CoInitializeEx(0, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    AudioSampleQueue queue;

    AudioCapSourceMF cap(std::bind(&AudioSampleQueue::put, &queue, std::placeholders::_1));
    AudioPlayMF play(std::bind(&AudioSampleQueue::get, &queue));

    cap.start();
    play.start();

    getchar();
    
    cap.stop();
    play.stop();

    MFShutdown();
    CoUninitialize();

    return 0;
}