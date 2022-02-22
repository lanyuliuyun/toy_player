
#ifndef AUDIO_CAP_SOURCE_ADV_H
#define AUDIO_CAP_SOURCE_ADV_H

#include <Dmo.h>

#include <inttypes.h>
#include <functional>
#include <thread>

/* 自带 AEC/NS/AGC 功能的音频采集，适合RTC场景
 */

class AudioCapSourceAdv
{
public:
    typedef std::function<void(int16_t* sample, int sample_count, int64_t pts)> AudioFrameSink;

    AudioCapSourceAdv(const AudioFrameSink sink, const wchar_t *mic_dev_name, const wchar_t *spk_dev_name);
    ~AudioCapSourceAdv();

    int getSpec(DWORD &sample_rate, DWORD &channles);

    int start();
    void stop();

private:
    void setup(const wchar_t *mic_dev_name, const wchar_t *spk_dev_name);
    void cap_routine();

    std::thread cap_worker_;
    bool worker_run_;
    AudioFrameSink sink_;

    IMediaObject *media_;
    int mic_dev_index_;
    int spk_dev_index_;

    uint8_t *audio_frame_output_;
    int audio_frame_buf_len_;

    AudioCapSourceAdv(const AudioCapSourceAdv&);
    AudioCapSourceAdv& operator=(const AudioCapSourceAdv&);
};

#endif /* AUDIO_CAP_SOURCE_ADV_H */
