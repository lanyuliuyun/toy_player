
#include "audio_cap_source_was.h"
#include "util.hpp"

#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Msdmo.lib")

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);

AudioCapSourceWAS::AudioCapSourceWAS(const AudioFrameSink sink, const wchar_t *spk_dev_name)
    : cap_worker_()
    , worker_run_(false)
    , sink_(sink)

    , sample_bits_(0)
    , sample_rate_(0)
    , channels_(0)
    , single_frame_size_(0)

    , silent_frame_(NULL)

    , data_evt_(NULL)
    , stop_evt_(NULL)
    , dev_change_evt_(NULL)

    , dev_enum_(NULL)
    , audio_client_(NULL)
    , audio_data_client_(NULL)

    , com_init_here_(false)
    , ref_count_(0)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hr == S_OK || hr == S_FALSE) { com_init_here_ = true; }

    CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dev_enum_));
    if (dev_enum_ == NULL) { return; }

    IMMDevice *audioin_dev = get_audioin_device(spk_dev_name);
    if (audioin_dev)
    {
        data_evt_ = CreateEventW(NULL, FALSE, FALSE, NULL);
        stop_evt_ = CreateEventW(NULL, FALSE, FALSE, NULL);
        dev_change_evt_ = CreateEventW(NULL, FALSE, FALSE, NULL);

        setup(audioin_dev);
        audioin_dev->Release();
    }
}

AudioCapSourceWAS::~AudioCapSourceWAS()
{
    delete[] silent_frame_;

    if (data_evt_ != NULL) { CloseHandle(data_evt_); }
    if (stop_evt_ != NULL) { CloseHandle(stop_evt_); }
    if (dev_change_evt_ != NULL) { CloseHandle(dev_change_evt_); }

    SafeRelease(audio_data_client_);
    SafeRelease(audio_client_);
    SafeRelease(dev_enum_);

    if (com_init_here_) { CoUninitialize(); }

    return;
}

int AudioCapSourceWAS::getSpec(DWORD &sample_bits, DWORD &sample_rate, DWORD &channels)
{
    if (audio_client_ == NULL) { return 0; }

    sample_bits = sample_bits_;
    sample_rate = sample_rate_;
    channels= channels_;

    return 1;
}

int AudioCapSourceWAS::start()
{
    if (audio_client_ == NULL) { return 0; }
    if (worker_run_) { return 1; }

    worker_run_ = true;
    cap_worker_ = std::thread(std::bind(&AudioCapSourceWAS::cap_routine, this));

    return 1;
}

void AudioCapSourceWAS::stop()
{
    if (worker_run_)
    {
        SetEvent(stop_evt_);
        cap_worker_.join();
    }

    return;
}

HRESULT STDMETHODCALLTYPE AudioCapSourceWAS::OnDefaultDeviceChanged(_In_  EDataFlow flow, _In_  ERole role, _In_  LPCWSTR pwstrDefaultDeviceId)
{
    if (flow == eCapture && role == eConsole)
    {
        SetEvent(dev_change_evt_);
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE AudioCapSourceWAS::QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject)
{
    if (ppvObject == NULL) { return E_POINTER; }

    *ppvObject = NULL;

    if (riid == IID_IUnknown)
    {
        *ppvObject = static_cast<IUnknown *>(this);
        AddRef();
    }
    else if (riid == __uuidof(IMMNotificationClient))
    {
        *ppvObject = static_cast<IMMNotificationClient *>(this);
        AddRef();
    }
    else { return E_NOINTERFACE; }

    return S_OK;
}

ULONG STDMETHODCALLTYPE AudioCapSourceWAS::AddRef( void)
{
    return InterlockedIncrement(&ref_count_);
}

ULONG STDMETHODCALLTYPE AudioCapSourceWAS::Release( void)
{
    ULONG ret_val = InterlockedDecrement(&ref_count_);
    if (ret_val == 0)
    {
        delete this;
    }
    return ret_val;
}

void AudioCapSourceWAS::setup(IMMDevice *audioin_dev)
{
    HRESULT hr;

    hr = audioin_dev->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<void **>(&audio_client_));

    WAVEFORMATEX *audio_format = NULL;
    hr = audio_client_->GetMixFormat(&audio_format);
    sample_bits_ = audio_format->wBitsPerSample;
    sample_rate_ = audio_format->nSamplesPerSec;
    channels_ = audio_format->nChannels;
    single_frame_size_ = (audio_format->wBitsPerSample / 8) * audio_format->nChannels;

    REFERENCE_TIME frame_size_ms = 20;
    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, (AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST), (frame_size_ms * 10000), 
        0, audio_format, NULL);

    CoTaskMemFree(audio_format);

    UINT32 max_frame_size = 0;
    audio_client_->GetBufferSize(&max_frame_size);
    silent_frame_ = new uint8_t[single_frame_size_ * max_frame_size];
    memset(silent_frame_, 0, (single_frame_size_ * max_frame_size));

    audio_client_->SetEventHandle(data_evt_);
    audio_client_->GetService(IID_PPV_ARGS(&audio_data_client_));
}

void AudioCapSourceWAS::cap_routine()
{
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    dev_enum_->RegisterEndpointNotificationCallback(this);

    audio_client_->Start();

    while (worker_run_)
    {
        HANDLE evts[] = {data_evt_, stop_evt_, dev_change_evt_};
        DWORD wait_ret = WaitForMultipleObjects(3, evts, FALSE, INFINITE);
        switch (wait_ret)
        {
            case WAIT_OBJECT_0:
            {
                do_data_cap();
                break;
            }
            case WAIT_OBJECT_0 + 1:
            {
                worker_run_ = false;
                break;
            }
            case WAIT_OBJECT_0 + 2:
            {
                handle_dev_change();
                break;
            }
            default: break;
        }
        if (audio_data_client_ == NULL) { break; }
    }

    dev_enum_->UnregisterEndpointNotificationCallback(this);

    if (audio_client_) { audio_client_->Stop(); }
    SafeRelease(audio_data_client_);

    CoUninitialize();

    return;
}

void AudioCapSourceWAS::do_data_cap()
{
    HRESULT hr;

    BYTE *data_ptr = NULL;
    UINT32 data_size = 0;
    DWORD flags = 0;
    UINT64 pts = 0;
    hr = audio_data_client_->GetBuffer(&data_ptr, &data_size, &flags, NULL, &pts);
    if (SUCCEEDED(hr) && data_size > 0)
    {
        bool silent = flags & AUDCLNT_BUFFERFLAGS_SILENT;
        if (silent)
        {
            sink_(silent_frame_, (int)(data_size * single_frame_size_), (pts / 10000), silent);
        }
        else
        {
            sink_((uint8_t*)data_ptr, (int)(data_size * single_frame_size_), (pts / 10000), silent);
        }
        audio_data_client_->ReleaseBuffer(data_size);
    }

    return;
}

void AudioCapSourceWAS::handle_dev_change()
{
    audio_client_->Stop();
    SafeRelease(audio_data_client_);
    SafeRelease(audio_client_);
    delete[] silent_frame_; silent_frame_ = NULL;

    IMMDevice *audioin_dev = get_audioin_device(NULL);
    if (audioin_dev)
    {
        setup(audioin_dev);
        audioin_dev->Release();

        audio_client_->Start();
    }

    return;
}

IMMDevice* AudioCapSourceWAS::get_audioin_device(const wchar_t *dev_name)
{
    IMMDevice *dev_selected = NULL;

    if (dev_name == NULL)
    {
        dev_enum_->GetDefaultAudioEndpoint(eCapture, eConsole, &dev_selected);
    }
    else
    {
        #if 0
        dev_enum_->GetDevice(dev_id, &dev);
        #else
        IMMDeviceCollection *dev_collects = NULL;
        dev_enum_->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &dev_collects);
        if (dev_collects == NULL)
        {
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

    return dev_selected;
}

#if defined(UT_SPEAKER_CAP_SOURCE)

#include <stdio.h>

int main(int argc, char *argv[])
{
    FILE *fp = fopen("audio_mic.pcm", "wb");

    AudioCapSourceWAS cap([&fp](uint8_t* sample_data, int sample_data_size, int64_t pts, bool silent){
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
