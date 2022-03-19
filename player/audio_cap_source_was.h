
#ifndef AUDIO_CAP_SOURCE_WAS
#define AUDIO_CAP_SOURCE_WAS

#include <mmdeviceapi.h>
#include <AudioClient.h>

#include <inttypes.h>
#include <functional>
#include <thread>

class AudioCapSourceWAS : public IMMNotificationClient
{
public:
    typedef std::function<void(uint8_t* sample_data, int sample_data_size, int64_t pts, bool silent)> AudioFrameSink;

    AudioCapSourceWAS(const AudioFrameSink sink, const wchar_t *spk_dev_name);
    ~AudioCapSourceWAS();

    int getSpec(DWORD &sample_bits, DWORD &sample_rate, DWORD &channles);

    int start();
    void stop();

    // IMMNotificationClient
    virtual HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(_In_  LPCWSTR pwstrDeviceId, _In_  DWORD dwNewState) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE OnDeviceAdded(_In_  LPCWSTR pwstrDeviceId) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE OnDeviceRemoved(_In_  LPCWSTR pwstrDeviceId) { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(_In_  EDataFlow flow, _In_  ERole role, _In_  LPCWSTR pwstrDefaultDeviceId);
    virtual HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(_In_  LPCWSTR pwstrDeviceId, _In_  const PROPERTYKEY key) { return S_OK; }

    // IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject);
    virtual ULONG STDMETHODCALLTYPE AddRef();
    virtual ULONG STDMETHODCALLTYPE Release();

private:
    void setup(IMMDevice *audioin_dev);
    void cap_routine();

    void do_data_cap();
    void handle_dev_change();

    std::thread cap_worker_;
    bool worker_run_;
    AudioFrameSink sink_;

    DWORD sample_bits_;
    DWORD sample_rate_;
    DWORD channels_;
    DWORD single_frame_size_;

    uint8_t *silent_frame_;

    HANDLE data_evt_;
    HANDLE stop_evt_;
    HANDLE dev_change_evt_;

    IMMDeviceEnumerator *dev_enum_;
    IAudioClient *audio_client_;
    IAudioCaptureClient *audio_data_client_;
    bool com_init_here_;
    LONG ref_count_;;

    IMMDevice* get_audioin_device(const wchar_t *dev_name);

    AudioCapSourceWAS(const AudioCapSourceWAS&);
    AudioCapSourceWAS& operator=(const AudioCapSourceWAS&);
};

#endif /* AUDIO_CAP_SOURCE_WAS */
