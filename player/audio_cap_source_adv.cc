
#include "audio_cap_source_adv.h"

#include <initguid.h>
#include <mmeapi.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <Wmcodecdsp.h>
#include <uuids.h>

DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395, 0xE52F, 0x467C, 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xA95664D2, 0x9614, 0x4F35, 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6);
DEFINE_GUID(IID_IMediaBuffer, 0x59eff8b9, 0x938c, 0x4a26, 0x82, 0xf2, 0x95, 0xcb, 0x84, 0xcd, 0xc8, 0x37);
DEFINE_GUID(IID_IMediaObject, 0xd8ad0f58, 0x5494, 0x4102, 0x97, 0xc5, 0xec, 0x79, 0x8e, 0x59, 0xbc, 0xf4);

class CBaseMediaBuffer : public IMediaBuffer {
public:
   CBaseMediaBuffer() {}
   CBaseMediaBuffer(BYTE *pData, ULONG ulSize, ULONG ulData) :
      m_pData(pData), m_ulSize(ulSize), m_ulData(ulData), m_cRef(1) {}

   STDMETHODIMP_(ULONG) AddRef() {
      return InterlockedIncrement((long*)&m_cRef);
   }

   STDMETHODIMP_(ULONG) Release() {
      long l = InterlockedDecrement((long*)&m_cRef);
      if (l == 0)
         delete this;
      return l;
   }

   STDMETHODIMP QueryInterface(REFIID riid, void **ppv) {
      if (riid == IID_IUnknown) {
         AddRef();
         *ppv = (IUnknown*)this;
         return NOERROR;
      }
      else if (riid == IID_IMediaBuffer) {
         AddRef();
         *ppv = (IMediaBuffer*)this;
         return NOERROR;
      }
      else
         return E_NOINTERFACE;
   }

   STDMETHODIMP SetLength(DWORD ulLength) {m_ulData = ulLength; return NOERROR;}

   STDMETHODIMP GetMaxLength(DWORD *pcbMaxLength) {*pcbMaxLength = m_ulSize; return NOERROR;}

   STDMETHODIMP GetBufferAndLength(BYTE **ppBuffer, DWORD *pcbLength) {
      if (ppBuffer) *ppBuffer = m_pData;
      if (pcbLength) *pcbLength = m_ulData;
      return NOERROR;
   }

protected:
   BYTE *m_pData;
   ULONG m_ulSize;
   ULONG m_ulData;
   ULONG m_cRef;
};

class CStaticMediaBuffer : public CBaseMediaBuffer {
public:
    CStaticMediaBuffer() {}
    ~CStaticMediaBuffer() {}

    STDMETHODIMP_(ULONG) AddRef() { return 2; }
    STDMETHODIMP_(ULONG) Release() { return 1; }

    void Init(BYTE *pData, ULONG ulSize, ULONG ulData) {
        m_pData = pData;
        m_ulSize = ulSize;
        m_ulData = ulData;
    }
};

#define VBFALSE (VARIANT_BOOL)0
#define VBTRUE  (VARIANT_BOOL)-1

AudioCapSourceAdv::AudioCapSourceAdv(const AudioFrameSink sink, const wchar_t *mic_dev_name, const wchar_t *spk_dev_name)
    : cap_worker_()
    , worker_run_(false)
    , sink_(sink)

    , media_(NULL)
{
    setup(mic_dev_name, spk_dev_name);
    return;
}

AudioCapSourceAdv::~AudioCapSourceAdv()
{
    if (media_) { media_->Release(); }
    return;
}

int AudioCapSourceAdv::getSpec(DWORD &sample_rate, DWORD &channles)
{
    if (media_ == NULL) { return 0; }

    sample_rate = 16000;
    channles = 1;

    return 0;
}

int AudioCapSourceAdv::start()
{
    if (media_ == NULL) { return 0; }
    if (worker_run_) { return 1; }

    HRESULT hr;

    IPropertyStore *media_prop = NULL;
    media_->QueryInterface(&media_prop);

    PROPVARIANT prov_val;
    PropVariantInit(&prov_val);
    prov_val.vt = VT_I4;
    prov_val.lVal = SINGLE_CHANNEL_AEC;
    media_prop->SetValue(MFPKEY_WMAAECMA_SYSTEM_MODE, prov_val);
    PropVariantClear(&prov_val);

    if (mic_dev_index_ >= 0 || spk_dev_index_ >= 0)
    {
        PropVariantInit(&prov_val);
        prov_val.vt = VT_I4;
        prov_val.lVal = (unsigned long)(spk_dev_index_<<16) + (unsigned long)(0x0000ffff & mic_dev_index_);
        media_prop->SetValue(MFPKEY_WMAAECMA_DEVICE_INDEXES, prov_val);
        PropVariantClear(&prov_val);
    }

    // 额外打开NS/AGC/center_clip
    {
        PropVariantInit(&prov_val);
        prov_val.vt = VT_BOOL;
        prov_val.boolVal = VBTRUE;
        media_prop->SetValue(MFPKEY_WMAAECMA_FEATURE_MODE, prov_val);
        PropVariantClear(&prov_val);

        PropVariantInit(&prov_val);
        prov_val.vt = VT_I4;
        prov_val.lVal = TRUE;
        media_prop->SetValue(MFPKEY_WMAAECMA_FEATR_NS, prov_val);
        PropVariantClear(&prov_val);

        PropVariantInit(&prov_val);
        prov_val.vt = VT_BOOL;
        prov_val.boolVal = VBTRUE;
        media_prop->SetValue(MFPKEY_WMAAECMA_FEATR_AGC, prov_val);
        PropVariantClear(&prov_val);

        PropVariantInit(&prov_val);
        prov_val.vt = VT_BOOL;
        prov_val.boolVal = VBTRUE;
        media_prop->SetValue(MFPKEY_WMAAECMA_FEATR_CENTER_CLIP, prov_val);
        PropVariantClear(&prov_val);
    }

    // 可惜支持的输出采样率只有 8000 / 11025 / 16000 / 22050 四档
    static const WAVEFORMATEX wave_format = {WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16, 0};

    DMO_MEDIA_TYPE dmo_media_type;
    MoInitMediaType(&dmo_media_type, sizeof(WAVEFORMATEX));
    dmo_media_type.majortype = MEDIATYPE_Audio;
    dmo_media_type.subtype = MEDIASUBTYPE_PCM;
    dmo_media_type.lSampleSize = 0;
    dmo_media_type.bFixedSizeSamples = TRUE;
    dmo_media_type.bTemporalCompression = FALSE;
    dmo_media_type.formattype = FORMAT_WaveFormatEx;
    WAVEFORMATEX *wav_fmt = (WAVEFORMATEX*)dmo_media_type.pbFormat;
    *wav_fmt = wave_format;

    hr = media_->SetOutputType(0, &dmo_media_type, 0);
    MoFreeMediaType(&dmo_media_type);

    hr = media_->AllocateStreamingResources();

    media_prop->Release();

    audio_frame_buf_len_ = wave_format.nSamplesPerSec * wave_format.nChannels * wave_format.nBlockAlign;
    audio_frame_output_ = new uint8_t[audio_frame_buf_len_];

    worker_run_ = true;
    cap_worker_ = std::thread(::std::bind(&AudioCapSourceAdv::cap_routine, this));

    return 1;
}

void AudioCapSourceAdv::stop()
{
    if (worker_run_)
    {
        worker_run_ = false;
        media_->Flush();
        cap_worker_.join();

        delete[] audio_frame_output_;
        audio_frame_buf_len_ = 0;
    }

    return;
}

void AudioCapSourceAdv::setup(const wchar_t *mic_dev_name, const wchar_t *spk_dev_name)
{
    HRESULT hr;

    IMMDeviceEnumerator *dev_enum = NULL;
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, IID_IMMDeviceEnumerator, (void**)&dev_enum);
    if (dev_enum == NULL) { return; }

    hr = CoCreateInstance(CLSID_CWMAudioAEC, NULL, CLSCTX_INPROC_SERVER, IID_IMediaObject, (void**)&media_);
    if (media_ == NULL)
    {
        dev_enum->Release();
        media_ = NULL;
        return;
    }

    /* 确定采集设备的index */
    int mic_dev_index = -1;
    if (mic_dev_name != NULL)
    {
        IMMDeviceCollection *dev_collects = NULL;
        dev_enum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &dev_collects);
        if (dev_collects)
        {
            UINT dev_count = 0;
            dev_collects->GetCount(&dev_count);
            for (UINT i = 0; i < dev_count; ++i)
            {
                IMMDevice *dev = NULL;
                dev_collects->Item(i, &dev);

                IPropertyStore *dev_prop = NULL;
                dev->OpenPropertyStore(STGM_READ, &dev_prop);

                PROPVARIANT prop_val;
                PropVariantInit(&prop_val);
                dev_prop->GetValue(PKEY_Device_FriendlyName, &prop_val);
                if (wcscmp(mic_dev_name, prop_val.pwszVal) == 0)
                {
                    mic_dev_index = (int)i;
                    PropVariantClear(&prop_val);
                    break;
                }
                PropVariantClear(&prop_val);
            }
            dev_collects->Release();
        }
    }

    /* 确定播音设备的index */
    int spk_dev_index = -1;
    if (spk_dev_name != NULL)
    {
        IMMDeviceCollection *dev_collects = NULL;
        dev_enum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &dev_collects);
        if (dev_collects)
        {
            UINT dev_count = 0;
            dev_collects->GetCount(&dev_count);
            for (UINT i = 0; i < dev_count; ++i)
            {
                IMMDevice *dev = NULL;
                dev_collects->Item(i, &dev);

                IPropertyStore *dev_prop = NULL;
                dev->OpenPropertyStore(STGM_READ, &dev_prop);

                PROPVARIANT prop_val;
                PropVariantInit(&prop_val);
                dev_prop->GetValue(PKEY_Device_FriendlyName, &prop_val);
                if (wcscmp(spk_dev_name, prop_val.pwszVal) == 0)
                {
                    spk_dev_index = (int)i;
                    PropVariantClear(&prop_val);
                    break;
                }
                PropVariantClear(&prop_val);
            }
            dev_collects->Release();
        }
    }

    dev_enum->Release();

    mic_dev_index_ = mic_dev_index;
    spk_dev_index_ = spk_dev_index;

    return;
}

void AudioCapSourceAdv::cap_routine()
{
    HRESULT hr;

    CStaticMediaBuffer media_buffer;
    DMO_OUTPUT_DATA_BUFFER dmo_data;
    dmo_data.pBuffer = &media_buffer;
    while (worker_run_)
    {
        media_buffer.Init((BYTE*)audio_frame_output_, audio_frame_buf_len_, 0);
        do
        {
            dmo_data.dwStatus = 0;

            DWORD dwStatus;
            DWORD data_size = 0;
            hr = media_->ProcessOutput(0, 1, &dmo_data, &dwStatus);
            if (hr == S_OK)
            {
                media_buffer.GetBufferAndLength(NULL, &data_size);
            }
            if (data_size > 0)
            {
                sink_((int16_t*)audio_frame_output_, (int)(data_size>>1), (dmo_data.rtTimestamp / 10000));
            }
        } while(dmo_data.dwStatus & DMO_OUTPUT_DATA_BUFFERF_INCOMPLETE);
    }

    return;
}

#if defined(UT_AUDIO_CAP_SOURCE_ADV)

int main(int argc, char *argv[])
{
    FILE *fp = fopen("audio.pcm", "wb");

    AudioCapSourceAdv cap([&fp](int16_t* sample, int sample_count, int64_t pts){
        fwrite(sample, sample_count, sizeof(int16_t), fp);
    }, NULL, NULL);

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
