
#include "audio_play_sink_mf.h"
#include "util.hpp"

#include <mfapi.h>
#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>

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

int AudioPlayMF::getSpec(DWORD &sample_bits, DWORD &sample_rate, DWORD &channles)
{
    if (stream_sink_ == NULL) { return 0; }

    sample_bits = 16;
    sample_rate = 48000;
    channles = 1;

    return 1;
}

int AudioPlayMF::start()
{
    if (stream_sink_ == NULL) { return -1; }

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

  #if 0
    printf("Enumerate all play media_type:\n");
    DWORD media_type_count = 0;
    media_type_handler->GetMediaTypeCount(&media_type_count);
    for (DWORD i = 0; i < media_type_count; ++i)
    {
        IMFMediaType *media_type = NULL;
        media_type_handler->GetMediaTypeByIndex(i, &media_type);
        /* 检查是否是所需规格 */
        media_type->Release();
    }
  #endif

  #if 1
    /* 实际上从输出设备支持的规格中选择一个，此处固定选择一个 48K/mono 规格 */
    DWORD sample_rate = 48000;
    DWORD channels = 1;
    IMFMediaType *input_media_type = NULL;
    {
        MFCreateMediaType(&input_media_type);
        input_media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        input_media_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        input_media_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
        input_media_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
        input_media_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        UINT32 block_align = channels * 2;
        input_media_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, block_align);
        UINT32 avg_bytes_per_sec = sample_rate * block_align;
        input_media_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, avg_bytes_per_sec);
        input_media_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

        printf("audio play spec: sample_rate: %u, channels: %u, block_align: %u, avg_bytes_per_sec: %u\n",
            sample_rate, channels, block_align, avg_bytes_per_sec);
    }
    hr = media_type_handler->SetCurrentMediaType(input_media_type);
    SafeRelease(input_media_type);
  #endif

    SafeRelease(media_type_handler);

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
    int frame_size = 48 * 20;
    LONGLONG sample_pts = 0;
    while (worker_run_)
    {
        IMFMediaEvent *event = NULL;
        HRESULT hr = stream_sink_->GetEvent(0, &event);
        if (hr != S_OK) { continue; }

        MediaEventType event_type;
        event->GetType(&event_type);
        switch (event_type)
        {
            case MEStreamSinkRequestSample:
            {
                IMFMediaBuffer *media_buf = NULL;
                MFCreateMemoryBuffer(frame_size * 2, &media_buf);

                BYTE *data = NULL;
                DWORD max_len = 0;
                media_buf->Lock(&data, &max_len, NULL);
                int ret = source_((int16_t*)data, frame_size);
                if (ret <= 0)
                {
                    memset(data, 0, (frame_size*2));
                }
                media_buf->Unlock();
                media_buf->SetCurrentLength((DWORD)ret * 2);

                IMFSample *sample = NULL;
                MFCreateSample(&sample);
                sample->AddBuffer(media_buf);
                media_buf->Release();

                LONGLONG duration = (ret / 48) * 10000;

                sample->SetSampleDuration(duration);
                sample->SetSampleFlags(0);
                sample->SetSampleTime(sample_pts);

                stream_sink_->ProcessSample(sample);
                sample->Release();

                sample_pts += duration;

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


#ifdef UT_AUDIO_PLAY_SINK_MF

#include <stdio.h>

int main(int argc, char *argv[])
{
    CoInitializeEx(0, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
    {
    FILE *fp = fopen(argv[1], "rb");

    auto file_source = [&fp](int16_t* samples, int max_sample_count) -> int{
        if (feof(fp)) { return 0; }
        int size = (int)fread(samples, 2, max_sample_count, fp);
        return size;
    };

    AudioPlayMF play(file_source, NULL);

    play.start();

    getchar();
    
    play.stop();

    fclose(fp);
    }
    MFShutdown();
    CoUninitialize();

    return 0;
}

#endif /* UT_AUDIO_PLAY_MF */
