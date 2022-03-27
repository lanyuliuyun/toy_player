
#include "audio_cap_source_wi.h"

#include <Windows.h>
#include <sysinfoapi.h>
#include <mmeapi.h>

#include <string.h>

AudioCapSourceWI::AudioCapSourceWI(const AudioFrameSink &sink, const char *dev_name)
    : cap_worker_()
    , worker_run_(false)
    , sink_(sink)
    , wave_data_evt_(NULL)
    , wave_dev_(NULL)

    , dev_index_(-1)
    , sample_rate_(0)
    , channels_(0)
{
    setup(dev_name);
    return;
}

AudioCapSourceWI::~AudioCapSourceWI()
{
    return;
}

int AudioCapSourceWI::getSpec(DWORD &sample_rate, DWORD &channles)
{
    if (dev_index_ < 0) { return 0; }

    sample_rate = sample_rate_;
    channles = channels_;

    return 1;
}

int AudioCapSourceWI::start()
{
    if (dev_index_ < 0) { return 0; }
    if (worker_run_) { return 1; }

    wave_data_evt_ = CreateEventW(NULL, FALSE, FALSE, NULL);

    MMRESULT res;

    WAVEFORMATEX wav_fmt;
    memset(&wav_fmt, 0, sizeof(wav_fmt));
    wav_fmt.wFormatTag = WAVE_FORMAT_PCM;
    wav_fmt.nChannels = (WORD)channels_;
    wav_fmt.nSamplesPerSec = sample_rate_;
    wav_fmt.nAvgBytesPerSec = sample_rate_ * 2;
    wav_fmt.nBlockAlign = 2;
    wav_fmt.wBitsPerSample = 16;
    wav_fmt.cbSize = 0;
    res = waveInOpen(&wave_dev_, 0, &wav_fmt, (DWORD_PTR)wave_data_evt_, NULL, CALLBACK_EVENT);
    if (res != MMSYSERR_NOERROR)
    {
        CloseHandle(wave_data_evt_);
        wave_data_evt_ = NULL;
        return 0;
    }

    worker_run_ = true;
    cap_worker_ = std::thread(std::bind(&AudioCapSourceWI::cap_routine, this));

    return 0;
}

void AudioCapSourceWI::stop()
{
    if (worker_run_)
    {
        worker_run_ = false;
        SetEvent(wave_data_evt_);
        cap_worker_.join();
        CloseHandle(wave_data_evt_);

        waveInClose(wave_dev_);
    }

    return;
}

void AudioCapSourceWI::setup(const char *dev_name)
{
    UINT dev_count = waveInGetNumDevs();
    if (dev_count == 0) { return; }
    
    enum {
        SR_48000 = 0,
        SR_44100 = 1,
        SR_22050 = 2,
        SR_11025 = 3
    };
    static struct wave_fmt {
        DWORD fmt;
        DWORD sample_rate;
    } prefer_wave_fmts[] = {
        {(WAVE_FORMAT_48M16 | WAVE_FORMAT_48S16), 48000},      // 48K
        {(WAVE_FORMAT_44M16 | WAVE_FORMAT_44S16 | WAVE_FORMAT_4S16 | WAVE_FORMAT_4M16), 44100},   // 44.1K
        {(WAVE_FORMAT_2M16 | WAVE_FORMAT_2S16), 22050},     // 22.05K
        {(WAVE_FORMAT_1S16 | WAVE_FORMAT_1M16), 11025}      // 11.025k
    };

    if (dev_name == NULL)
    {
        WAVEINCAPSA caps;
        MMRESULT res = waveInGetDevCapsA(0, &caps, sizeof(caps));
        if (res != MMSYSERR_NOERROR) { return; }

        if (caps.dwFormats & prefer_wave_fmts[SR_48000].fmt)
        {
            dev_index_ = 0;
            sample_rate_ = 48000;
            channels_ = 1;
        }
        else if (caps.dwFormats & prefer_wave_fmts[SR_44100].fmt)
        {
            dev_index_ = 0;
            sample_rate_ = 44100;
            channels_ = 1;
        }
        else if (caps.dwFormats & prefer_wave_fmts[SR_22050].fmt)
        {
            dev_index_ = 0;
            sample_rate_ = 22050;
            channels_ = 1;
        }
        else if (caps.dwFormats & prefer_wave_fmts[SR_11025].fmt)
        {
            dev_index_ = 0;
            sample_rate_ = 11025;
            channels_ = 1;
        }
    }
    else
    {
        for (UINT i = 0; i < dev_count; ++i)
        {
            WAVEINCAPSA caps;
            MMRESULT res = waveInGetDevCapsA(i, &caps, sizeof(caps));
            if (res != MMSYSERR_NOERROR) { continue; }

            if (strcmp(caps.szPname, dev_name) != 0) { continue; }

            if (caps.dwFormats & prefer_wave_fmts[SR_48000].fmt)
            {
                dev_index_ = i;
                sample_rate_ = 48000;
                channels_ = 1;
                break;
            }
            else if (caps.dwFormats & prefer_wave_fmts[SR_44100].fmt)
            {
                dev_index_ = i;
                sample_rate_ = 44100;
                channels_ = 1;
                break;
            }
            else if (caps.dwFormats & prefer_wave_fmts[SR_22050].fmt)
            {
                dev_index_ = i;
                sample_rate_ = 22050;
                channels_ = 1;
                break;
            }
            else if (caps.dwFormats & prefer_wave_fmts[SR_11025].fmt)
            {
                dev_index_ = i;
                sample_rate_ = 11025;
                channels_ = 1;
                break;
            }
        }
    }

    return;
}

void AudioCapSourceWI::cap_routine()
{
    int audio_frame_size = 2 * (sample_rate_ / 50) * channels_;
    std::unique_ptr<int16_t[]> audio_frame_data(new int16_t[audio_frame_size * 4]);
    int16_t *audio_frame_output = audio_frame_data.get();
    int16_t *audio_frame_data_ptrs[3] = {
        audio_frame_output + audio_frame_size,
        audio_frame_output + audio_frame_size * 2,
        audio_frame_output + audio_frame_size * 3,
    };
    
    MMRESULT res;

    WAVEHDR wav_frames[3];
    for (int i = 0; i < 3; ++i)
    {
        memset(&wav_frames[i], 0, sizeof(wav_frames[i]));
        wav_frames[i].lpData = (LPSTR)audio_frame_data_ptrs[i];
        wav_frames[i].dwBufferLength = audio_frame_size;
        wav_frames[i].dwUser = (DWORD_PTR)i;
        wav_frames[i].dwFlags = 0;
        res = waveInPrepareHeader(wave_dev_, &wav_frames[i], sizeof(wav_frames[i]));
        res = waveInAddBuffer(wave_dev_, &wav_frames[i], sizeof(wav_frames[i]));
    }
    res = waveInStart(wave_dev_);
    
    int buffer_index = 0;
    int64_t pts = 0;
    while (worker_run_)
    {
        WaitForSingleObject(wave_data_evt_, INFINITE);
        if (!worker_run_) { break; }

        WAVEHDR *wav_frame = &wav_frames[buffer_index];
        if ((wav_frame->dwFlags & WHDR_DONE) && (wav_frame->dwBytesRecorded > 0))
        {
            int samples = wav_frame->dwBytesRecorded>>1;
            memcpy(audio_frame_output, wav_frame->lpData, wav_frame->dwBytesRecorded);
            waveInUnprepareHeader(wave_dev_, wav_frame, sizeof(*wav_frame));

            wav_frame = &wav_frames[buffer_index];
            memset(wav_frame, 0, sizeof(*wav_frame));
            wav_frame->lpData = (LPSTR)audio_frame_data_ptrs[buffer_index];
            wav_frame->dwBufferLength = audio_frame_size;
            wav_frame->dwUser = (DWORD_PTR)buffer_index;
            wav_frame->dwFlags = 0;
            res = waveInPrepareHeader(wave_dev_, wav_frame, sizeof(*wav_frame));
            res = waveInAddBuffer(wave_dev_, wav_frame, sizeof(*wav_frame));

            buffer_index++;
            if (buffer_index == 3) { buffer_index = 0; }

            sink_(audio_frame_output, samples, pts);
            pts += ((1000 * samples) / channels_) / sample_rate_;
        }
    }

    res = waveInReset(wave_dev_);
    res = waveInStop(wave_dev_);

    return;
}

#if defined(UT_AUDIO_CAP_SOURCE_WI)

#include <stdio.h>

int main(int argc, char *argv[])
{
    FILE *fp = fopen(argv[1], "wb");

    AudioCapSourceWI cap([&fp](int16_t* sample, int sample_count, int64_t pts){
        fwrite(sample, sample_count, sizeof(int16_t), fp);
    }, NULL);

    DWORD sample_rate, channels;
    if (cap.getSpec(sample_rate, channels))
    {
        printf("audio output spec: sample_rate: %u, channels: %u\n", sample_rate, channels);
    }

    cap.start();

    getchar();

    cap.stop();

    fclose(fp);

    printf("=== quits ===");

    return 0;
}

#endif
