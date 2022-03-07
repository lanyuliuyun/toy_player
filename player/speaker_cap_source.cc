
#include "speaker_cap_source.h"

#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Msdmo.lib")

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);

SpeakerCapSource::SpeakerCapSource(const AudioFrameSink sink, const wchar_t *spk_dev_name)
    : cap_worker_()
    , worker_run_(false)
    , sink_(sink)

    , sample_bits_(0)
    , sample_rate_(0)
    , channels_(0)
    , single_frame_size_(0)

    , data_evt_(NULL)
    , stop_evt_(NULL)

    , audio_client_(NULL)

    , com_init_here_(false)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == S_OK || hr == S_FALSE) { com_init_here_ = true; }

    setup(spk_dev_name);
}

SpeakerCapSource::~SpeakerCapSource()
{
    if (data_evt_ != NULL) { CloseHandle(data_evt_); }
    if (stop_evt_ != NULL) { CloseHandle(stop_evt_); }

    if (audio_client_ != NULL) { audio_client_->Release(); }

    if (com_init_here_) { CoUninitialize(); }

    return;
}

int SpeakerCapSource::getSpec(DWORD &sample_bits, DWORD &sample_rate, DWORD &channels)
{
    if (audio_client_ == NULL) { return 0; }

    sample_bits = sample_bits_;
    sample_rate = sample_rate_;
    channels= channels_;

    return 1;
}

int SpeakerCapSource::start()
{
    if (audio_client_ == NULL) { return 0; }
    if (worker_run_) { return 1; }

    worker_run_ = true;
    cap_worker_ = std::thread(std::bind(&SpeakerCapSource::cap_routine, this));

    return 1;
}

void SpeakerCapSource::stop()
{
    if (worker_run_)
    {
        worker_run_ = false;
        SetEvent(stop_evt_);
        cap_worker_.join();
    }

    return;
}

void SpeakerCapSource::setup(const wchar_t *spk_dev_name)
{
    HRESULT hr;

    IMMDevice *spk_dev = get_speaker_device(spk_dev_name);
    if (spk_dev == NULL) { return; }

    hr = spk_dev->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<void **>(&audio_client_));
    spk_dev->Release();

    WAVEFORMATEX *audio_format = NULL;
    hr = audio_client_->GetMixFormat(&audio_format);
    sample_bits_ = audio_format->wBitsPerSample;
    sample_rate_ = audio_format->nSamplesPerSec;
    channels_ = audio_format->nChannels;
    single_frame_size_ = (audio_format->wBitsPerSample / 8) * audio_format->nChannels;

    REFERENCE_TIME frame_size_ms = 20;
    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, (AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST), (frame_size_ms * 10000), 
        0, audio_format, NULL);

    CoTaskMemFree(audio_format);

    data_evt_ = CreateEventW(NULL, FALSE, FALSE, NULL);
    audio_client_->SetEventHandle(data_evt_);

    stop_evt_ = CreateEventW(NULL, FALSE, FALSE, NULL);
}

void SpeakerCapSource::cap_routine()
{
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    IAudioCaptureClient *audio_data_client = NULL;
    hr = audio_client_->GetService(IID_PPV_ARGS(&audio_data_client));

    audio_client_->Start();

    uint8_t *silent_frame = new uint8_t[single_frame_size_ * sample_rate_];
    memset(silent_frame, 0, (single_frame_size_ * sample_rate_));

    while (worker_run_)
    {
        HANDLE evts[] = {data_evt_, stop_evt_};
        DWORD wait_ret = WaitForMultipleObjects(2, evts, FALSE, INFINITE);
        switch (wait_ret)
        {
            case WAIT_OBJECT_0:
            {
                BYTE *data_ptr = NULL;
                UINT32 data_size = 0;
                DWORD flags = 0;
                UINT64 pts = 0;
                hr = audio_data_client->GetBuffer(&data_ptr, &data_size, &flags, NULL, &pts);
                if (SUCCEEDED(hr) && data_size > 0)
                {
                    bool silent = flags & AUDCLNT_BUFFERFLAGS_SILENT;
                    if (silent)
                    {
                        sink_(silent_frame, (int)(data_size * single_frame_size_), (pts / 10000), silent);
                    }
                    else
                    {
                        sink_((uint8_t*)data_ptr, (int)(data_size * single_frame_size_), (pts / 10000), silent);
                    }
                    audio_data_client->ReleaseBuffer(data_size);
                }
                break;
            }
            case WAIT_OBJECT_0 + 1:
            {
                worker_run_ = false;
                break;
            }
            default: break;
        }
    }

    delete[] silent_frame;

    audio_client_->Stop();

    audio_data_client->Release();

    CoUninitialize();

    return;
}

IMMDevice* SpeakerCapSource::get_speaker_device(const wchar_t *dev_name)
{
    //HRESULT hr;

    IMMDevice *dev_selected = NULL;

    IMMDeviceEnumerator *dev_enum = NULL;
    CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, IID_IMMDeviceEnumerator, (void**)&dev_enum);
    if (dev_enum == NULL)
    {
        return NULL;
    }

    if (dev_name == NULL)
    {
        dev_enum->GetDefaultAudioEndpoint(eRender, eConsole, &dev_selected);
        //dev_enum->GetDefaultAudioEndpoint(eCapture, eConsole, &dev_selected);
    }
    else
    {
        #if 0
        dev_enum->GetDevice(dev_id, &dev);
        #else
        IMMDeviceCollection *dev_collects = NULL;
        dev_enum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &dev_collects);
        if (dev_collects == NULL)
        {
            dev_enum->Release();
            return NULL;
        }

        UINT dev_count = 0;
        dev_collects->GetCount(&dev_count);
        //printf("active capture device, count: %u\n", dev_count);
        for (UINT i = 0; i < dev_count; ++i)
        {
            IMMDevice *dev = NULL;
            dev_collects->Item(i, &dev);

            IPropertyStore *dev_prop = NULL;
            dev->OpenPropertyStore(STGM_READ, &dev_prop);

            PROPVARIANT prop_val;
            PropVariantInit(&prop_val);
            dev_prop->GetValue(PKEY_Device_FriendlyName, &prop_val);

            if (wcscmp(prop_val.pwszVal, dev_name) == 0)
            {
                dev_selected = dev;
            }
            else
            {
                dev->Release();
            }
            PropVariantClear(&prop_val);

            dev_prop->Release();

            if (dev_selected != NULL) { break; }
        }
        dev_collects->Release();
        #endif
    }
    dev_enum->Release();

    return dev_selected;
}

#if defined(UT_SPEAKER_CAP_SOURCE)

#include <stdio.h>

int main(int argc, char *argv[])
{
    FILE *fp = fopen("audio_spk.pcm", "wb");

    SpeakerCapSource cap([&fp](uint8_t* sample_data, int sample_data_size, int64_t pts, bool silent){
        //printf("audio-frame, data_size: %d, pts: %" PRId64 "ms\n", sample_data_size, pts);
        fwrite(sample_data, sample_data_size, 1, fp);
    }, NULL);

    DWORD sample_bits, sample_rate, channles;
    if (cap.getSpec(sample_bits, sample_rate, channles))
    {
        printf("sample_bits: %u, sample_rate: %u, channles: %u\n", sample_bits, sample_rate, channles);
    }

    cap.start();

    getchar();

    cap.stop();

    fclose(fp);

    printf("=== quit ===\n");

    return 0;
}

#endif
