
#ifndef AUDIO_CAP_SOURCE_WAS
#define AUDIO_CAP_SOURCE_WAS

#include <mmdeviceapi.h>
#include <AudioClient.h>

#include <inttypes.h>
#include <functional>
#include <thread>

class AudioPlaySinkWAS : public IMMNotificationClient
{
public:
    typedef std::function<int(uint8_t* sample, int max_sample_count, int sample_size)> AudioDataSource;

    AudioPlaySinkWAS(const AudioDataSource &source, const wchar_t *spk_dev_name);
    ~AudioPlaySinkWAS();

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
    void setup(IMMDevice *audio_out_dev);
    void play_routine();

    void do_data_play();
    void handle_dev_change();

    std::thread play_worker_;
    bool worker_run_;
    AudioDataSource source_;

    DWORD sample_bits_;
    DWORD sample_rate_;
    DWORD channels_;
    int single_frame_size_;
    int max_buffer_len_;

    HANDLE data_evt_;
    HANDLE stop_evt_;
    HANDLE dev_change_evt_;

    IMMDeviceEnumerator *dev_enum_;
    IAudioClient *audio_client_;
    IAudioRenderClient *audio_data_client_;
    bool com_init_here_;
    LONG ref_count_;;

    IMMDevice* get_audioout_device(const wchar_t *dev_name);

    AudioPlaySinkWAS(const AudioPlaySinkWAS&);
    AudioPlaySinkWAS& operator=(const AudioPlaySinkWAS&);
};

#endif /* AUDIO_CAP_SOURCE_WAS */
