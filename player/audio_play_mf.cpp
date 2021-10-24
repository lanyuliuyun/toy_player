
#include "audio_play_mf.h"
#include "util.hpp"

#include <mfapi.h>
#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>

AudioPlayMF::AudioPlayMF(AudioFrameSource source) : AudioPlayMF(source, NULL)
{
}

AudioPlayMF::AudioPlayMF(AudioFrameSource source, const wchar_t *dev_name)
    : play_worker_()
    , worker_run_(false)
    , source_(source)

    , present_clock_(NULL)
    , sink_(NULL)
    , stream_sink_(NULL)
    , stream_sink_index_(NULL)
    , sink_spec_(0)
{
    setup(dev_name);
}

AudioPlayMF::~AudioPlayMF()
{
    if (present_clock_)
    {
        present_clock_->Stop();
        present_clock_->Release();
        present_clock_ = NULL;
    }

    if ((sink_spec_ & MEDIASINK_FIXED_STREAMS) == 0) { sink_->RemoveStreamSink(stream_sink_index_); }
    SafeRelease(stream_sink_);
    SafeRelease(sink_);

    return;
}

int AudioPlayMF::start()
{
    if (stream_sink_ == NULL)
    {
        return -1;
    }
    worker_run_ = true;
    play_worker_ = std::thread(std::bind(&AudioPlayMF::play_routine, this));

    return 0;
}

void AudioPlayMF::stop()
{
    if (worker_run_)
    {
        worker_run_ = false;
        if (stream_sink_ != NULL)
        {
            stream_sink_->QueueEvent(MEExtendedType, GUID_NULL, 0, NULL);
        }
        play_worker_.join();
    }

    return;
}

void AudioPlayMF::setup(const wchar_t *dev_name)
{
    HRESULT hr;

    IMMDeviceEnumerator *sar_enum = NULL;
    IMMDeviceCollection *sars = NULL;
    IMMDevice *device = NULL;
    do
    {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&sar_enum);
        if (hr != S_OK)
        {
            break;
        }

        hr = sar_enum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &sars);
        if (hr != S_OK)
        {
            break;
        }

        UINT sars_count = 0;
        hr = sars->GetCount(&sars_count);
        if (sars_count == 0)
        {
            break;
        }

        if (dev_name == NULL)
        {
            hr = sars->Item(0, &device);
        }
        else
        {
            for (UINT i = 0; i < sars_count; i++)
            {
                hr = sars->Item(i, &device);
                if (hr != S_OK)
                {
                    device = NULL;
                    continue;
                }

                IPropertyStore *prop = NULL;
                hr = device->OpenPropertyStore(STGM_READ, &prop);
                if (prop != NULL)
                {
                    PROPVARIANT var;
                    PropVariantInit(&var);
                    hr = prop->GetValue(PKEY_Device_FriendlyName, &var);
                    if (hr == S_OK)
                    {
                        if (wcscmp(var.pwszVal, dev_name) == 0)
                        {
                            PropVariantClear(&var);
                            prop->Release();
                            break;
                        }
                    }
                    PropVariantClear(&var);
                    prop->Release();
                }
                SafeRelease(device);
            }
        }
    } while(0);

	SafeRelease(sar_enum);
	SafeRelease(sars);

    if (device == NULL) { return; }

    IMFMediaSink *audio_sink = NULL;
    LPWSTR wstrID = NULL;
    hr = device->GetId(&wstrID);

    IMFAttributes *attr = NULL;
    hr = MFCreateAttributes(&attr, 1);
    hr = attr->SetString(MF_AUDIO_RENDERER_ATTRIBUTE_ENDPOINT_ID, wstrID);
    hr = MFCreateAudioRenderer(attr, &audio_sink); 
    attr->Release();
    CoTaskMemFree(wstrID);
    SafeRelease(device);

    /**************************************************/

    IMFPresentationClock *present_clock = NULL;
    hr = MFCreatePresentationClock(&present_clock);
    IMFPresentationTimeSource *clock_source = NULL;
    MFCreateSystemTimeSource(&clock_source);
    hr = present_clock->SetTimeSource(clock_source);
    SafeRelease(clock_source);

    hr = audio_sink->SetPresentationClock(present_clock);
    
    /**************************************************/

    DWORD sink_spec = 0;
    audio_sink->GetCharacteristics(&sink_spec);
    
    DWORD sink_stream_index = (DWORD)-1;
    IMFStreamSink *stream_sink = NULL;
    if (sink_spec & MEDIASINK_FIXED_STREAMS)
    {
        sink_stream_index = 0;
        hr = audio_sink->GetStreamSinkByIndex(0, &stream_sink);
    }
    else
    {
        sink_stream_index = 0;
        hr = audio_sink->AddStreamSink(0, NULL, &stream_sink);
    }

    IMFMediaTypeHandler *media_type_handler = NULL;
    hr = stream_sink->GetMediaTypeHandler(&media_type_handler);
    IMFMediaType *input_media_type = NULL;
    {
        MFCreateMediaType(&input_media_type);
        input_media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        input_media_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        input_media_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1);
        input_media_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
        input_media_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 2);
        input_media_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 96000);
        input_media_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        input_media_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    }
    hr = media_type_handler->SetCurrentMediaType(input_media_type);
    SafeRelease(media_type_handler);
    SafeRelease(input_media_type);
    
    IMFMediaSinkPreroll *media_sink_preroll = NULL;
    if (sink_spec & MEDIASINK_CAN_PREROLL)
    {
        hr = audio_sink->QueryInterface(IID_PPV_ARGS(&media_sink_preroll));
    }
    if (media_sink_preroll != NULL)
    {
        media_sink_preroll->NotifyPreroll(1000000);
        media_sink_preroll->Release();
    }
    else
    {
        present_clock->Start(1000000);
    }
    
    /**************************************************/

    present_clock_ = present_clock;
    sink_ = audio_sink;
    stream_sink_ = stream_sink;
    stream_sink_index_ = sink_stream_index;
    sink_spec_ = sink_spec;

    return;
}

void AudioPlayMF::play_routine()
{
    HRESULT hr;
    while (worker_run_)
    {
        IMFMediaEvent *event = NULL;
        hr = stream_sink_->GetEvent(0, &event);
        if (hr != S_OK) { continue; }

        MediaEventType event_type;
        event->GetType(&event_type);
        switch (event_type)
        {
            case MEStreamSinkRequestSample:
            {
                IMFSample *sample = source_();
                if (sample != NULL)
                {
                    stream_sink_->ProcessSample(sample);
                    sample->Release();
                }
                break;
            }
            case MEStreamSinkPrerolled:
            {
                present_clock_->Start(1000000);
                break;
            }
            default: break;
        }

        event->Release();
    }

    return;
}


#ifdef UT_AUDIO_PLAY_MF

#include <stdio.h>

int wmain(int argc, wchar_t *argv[])
{
    CoInitializeEx(0, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    FILE *fp = _wfopen(argv[1], L"rb");
    if (fp == NULL)
    {
        MFShutdown();
        CoUninitialize();

        return 0;
    }

    LONGLONG sample_pts = 0;
    auto file_source = [&fp, &sample_pts]() -> IMFSample*{
        if (feof(fp)) { return NULL; }

        IMFSample *sample = NULL;
        MFCreateSample(&sample);
        IMFMediaBuffer *media_buf = NULL;
        MFCreateMemoryBuffer(960, &media_buf);

        BYTE *data = NULL;
        DWORD max_len = 0;
        media_buf->Lock(&data, &max_len, NULL);
        int size = fread(data, 1, max_len, fp);
        media_buf->Unlock();
        media_buf->SetCurrentLength(size);
        sample->AddBuffer(media_buf);
        media_buf->Release();

        LONGLONG duration = 10000 * (size / 96);
        sample->SetSampleDuration(duration);
        sample->SetSampleFlags(0);
        sample->SetSampleTime(sample_pts);

        sample_pts += duration;

        return sample;
    };

    AudioPlayMF play(file_source);

    play.start();

    getchar();
    
    play.stop();

    fclose(fp);

    MFShutdown();
    CoUninitialize();

    return 0;
}

#endif /* UT_AUDIO_PLAY_MF */
