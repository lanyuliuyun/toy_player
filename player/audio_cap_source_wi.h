
#ifndef AUDIO_CAP_SOURCE_WI_H
#define AUDIO_CAP_SOURCE_WI_H

#include <windows.h>
#include <mmeapi.h>

#include <inttypes.h>
#include <functional>
#include <thread>

class AudioCapSourceWI
{
public:
    typedef std::function<void(int16_t* sample, int sample_count, int64_t pts)> AudioFrameSink;
    
    AudioCapSourceWI(const AudioFrameSink sink, const char *dev_name);

    ~AudioCapSourceWI();

    int getSpec(DWORD &sample_rate, DWORD &channles);

    int start();
    void stop();

private:
    void setup(const char *dev_name);
    void cap_routine();

    std::thread cap_worker_;
    bool worker_run_;
    AudioFrameSink sink_;
    HANDLE wave_data_evt_;
    HWAVEIN wave_dev_;

    int dev_index_;
    DWORD sample_rate_;
    DWORD channels_;
    int16_t audio_frame_[48 * 20];

    AudioCapSourceWI(const AudioCapSourceWI&);
    AudioCapSourceWI& operator=(const AudioCapSourceWI&);
};

#endif // AUDIO_CAP_SOURCE_WI_H