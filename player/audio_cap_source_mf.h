
#ifndef AUDIO_CAP_SOURCE_MF_H
#define AUDIO_CAP_SOURCE_MF_H

#include <Windows.h>
#include <Mfidl.h>
#include <mfreadwrite.h>

#include <inttypes.h>
#include <functional>
#include <thread>

class AudioCapSourceMF
{
public:
    typedef std::function<void(int16_t* sample, int sample_count, int64_t pts)> AudioFrameSink;

    AudioCapSourceMF(const AudioFrameSink sink, const wchar_t *dev_name);

    ~AudioCapSourceMF();

    int getSpec(DWORD *sample_rate, DWORD *channles);

    int start();
    void stop();

private:
    void setup(const wchar_t *dev_name);
    void cap_routine();

    std::thread cap_worker_;
    bool worker_run_;
    AudioFrameSink sink_;

    IMFMediaSource *source_;
    IMFSourceReader *reader_;
    DWORD audio_stream_index_;
    DWORD sample_rate_;
    DWORD channels_;

    int16_t audio_frame_[48 * 500];

    AudioCapSourceMF(const AudioCapSourceMF&);
    AudioCapSourceMF& operator=(const AudioCapSourceMF&);
};

#endif /* !AUDIO_CAP_SOURCE_MF_H */
