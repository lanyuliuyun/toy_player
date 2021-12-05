
#ifndef AUDIO_CAP_SOURCE_MF_H
#define AUDIO_CAP_SOURCE_MF_H

#include <Windows.h>
#include <Mfidl.h>
#include <mfreadwrite.h>

#include <functional>
#include <thread>

class AudioCapSourceMF
{
public:
    typedef std::function<void(IMFSample*)> AudioFrameSink;

    AudioCapSourceMF(const AudioFrameSink sink);
    explicit AudioCapSourceMF(const AudioFrameSink sink, const wchar_t *dev_name);

    ~AudioCapSourceMF();

    int getSpec(DWORD *sample_rate, DWORD *channles, DWORD *sample_bits);

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
    DWORD sample_bits_;

    AudioCapSourceMF(const AudioCapSourceMF&);
    AudioCapSourceMF& operator=(const AudioCapSourceMF&);
};

#endif /* !AUDIO_CAP_SOURCE_MF_H */
