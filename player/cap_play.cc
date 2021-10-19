
#include "audio_cap_source_mf.h"
#include "audio_play_mf.h"

#include <list>
#include <mutex>
#include <stdio.h>

int wmain(int argc, wchar_t *argv[])
{
    std::list<IMFSample*> samples;
    std::mutex samples_lock;

    AudioCapSourceMF cap([&samples, &samples_lock](IMFSample* sample){
        std::lock_guard<std::mutex> guard(samples_lock);
        samples.push_back(sample);
    });

    AudioPlayMF play([&samples, &samples_lock]() -> IMFSample* {
        IMFSample *sample = NULL;
        std::lock_guard<std::mutex> guard(samples_lock);
        if (!samples.empty())
        {
            sample = samples.front();
            samples.pop_front();
        }
        return sample;
    });
    
    play.start();
    cap.start();
    
    getchar();
    
    cap.stop();
    play.start();
    
    for (IMFSample* s : samples)
    {
        s->Release();
    }
    samples.clear();

    return 0;
}