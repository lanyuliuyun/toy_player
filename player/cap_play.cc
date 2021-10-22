
#include "audio_cap_source_mf.h"
#include "audio_play_mf.h"

#include <Objbase.h>
#include <mfapi.h>

#include <list>
#include <mutex>
#include <functional>

class AudioSampleQueue
{
public:
    AudioSampleQueue() {}
    ~AudioSampleQueue() {
        for (IMFSample* s : samples_)
        {
            s->Release();
        }
        samples_.clear();
    }

    void put(IMFSample* sample)
    {
        std::lock_guard<std::mutex> guard(samples_lock_);
        sample->AddRef();
        samples_.push_back(sample);
    }

    IMFSample* get()
    {
        IMFSample *sample = NULL;
        std::lock_guard<std::mutex> guard(samples_lock_);
        if (!samples_.empty())
        {
            sample = samples_.front();
            samples_.pop_front();
        }
        return sample;
    }
private:
    std::list<IMFSample*> samples_;
    std::mutex samples_lock_;
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