
#ifndef SPEAKER_CAP_SOURCE_H
#define SPEAKER_CAP_SOURCE_H

#include <mmdeviceapi.h>
#include <AudioClient.h>

#include <inttypes.h>
#include <functional>
#include <thread>

/* 抓取声卡的播音数据 */

class SpeakerCapSource
{
public:
    typedef std::function<void(uint8_t* sample_data, int sample_data_size, int64_t pts, bool silent)> AudioFrameSink;

    SpeakerCapSource(const AudioFrameSink sink, const wchar_t *spk_dev_name);
    ~SpeakerCapSource();

    int getSpec(DWORD &sample_bits, DWORD &sample_rate, DWORD &channles);

    int start();
    void stop();

private:
    void setup(const wchar_t *spk_dev_name);
    void cap_routine();

    std::thread cap_worker_;
    bool worker_run_;
    AudioFrameSink sink_;

    DWORD sample_bits_;
    DWORD sample_rate_;
    DWORD channels_;
    DWORD single_frame_size_;

    HANDLE data_evt_;
    HANDLE stop_evt_;

    IAudioClient *audio_client_;
    bool com_init_here_;

    IMMDevice* get_speaker_device(const wchar_t *dev_name);

    SpeakerCapSource(const SpeakerCapSource&);
    SpeakerCapSource& operator=(const SpeakerCapSource&);
};

#endif /* SPEAKER_CAP_SOURCE_H */
