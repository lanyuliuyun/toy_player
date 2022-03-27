
#ifndef AUDIO_PLAY_MF_H
#define AUDIO_PLAY_MF_H

#include <Windows.h>
#include <Mfidl.h>

#include <functional>
#include <thread>

class AudioPlayMF
{
public:
    typedef std::function<int(int16_t* samples, int max_sample_count)> AudioFrameSource;
    AudioPlayMF(AudioFrameSource source, const wchar_t *dev_name);
    ~AudioPlayMF();

    int getSpec(DWORD &sample_bits, DWORD &sample_rate, DWORD &channles);

    int start();
    void stop();

private:
    void setup(const wchar_t *dev_name);
    void play_routine();

    std::thread play_worker_;
    bool worker_run_;
    AudioFrameSource source_;

    IMFPresentationClock *present_clock_;
    IMFMediaSink *sink_;
    IMFStreamSink *stream_sink_;
    DWORD stream_sink_index_;
    DWORD sink_spec_;

    AudioPlayMF(const AudioPlayMF&);
    AudioPlayMF& operator=(const AudioPlayMF&);
};

#endif /* AUDIO_PLAY_MF_H */
