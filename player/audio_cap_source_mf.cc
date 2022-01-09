
#include "audio_cap_source_mf.h"
#include "util.hpp"

#include <mfapi.h>
#include <Mferror.h>

AudioCapSourceMF::AudioCapSourceMF(const AudioFrameSink sink, const wchar_t *dev_name)
    : cap_worker_()
    , worker_run_(false)
    , sink_(sink)

    , source_(NULL)
    , reader_(NULL)
    , audio_stream_index_((DWORD)-1)
    , sample_rate_(0)
    , channels_(0)
{
    setup(dev_name);
    return;
}

AudioCapSourceMF::~AudioCapSourceMF()
{
    SafeRelease(source_);
    SafeRelease(reader_);

    return;
}

int AudioCapSourceMF::start()
{
    HRESULT hr;
    if (source_ == NULL || reader_ == NULL)
    {
        return -1;
    }
    
	IMFPresentationDescriptor *desc = NULL;
	hr = source_->CreatePresentationDescriptor(&desc);

	PROPVARIANT var;
	PropVariantInit(&var);
	var.vt = VT_EMPTY;
	hr = source_->Start(desc, NULL, &var);
    SafeRelease(desc);
    
    worker_run_ = true;
    cap_worker_ = std::thread(std::bind(&AudioCapSourceMF::cap_routine, this));

    return 0;
}

void AudioCapSourceMF::stop()
{
    if (worker_run_)
    {
        worker_run_ = false;
        cap_worker_.join();
    }

    return;
}

void AudioCapSourceMF::setup(const wchar_t *dev_name)
{
    HRESULT hr;
	IMFMediaSource *source = NULL;

	IMFAttributes *mf_config = NULL;
	hr = MFCreateAttributes(&mf_config, 1);

	hr = mf_config->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID);

	IMFActivate **mf_devices = NULL;
	UINT32 mf_devices_count = 0;
	hr = MFEnumDeviceSources(mf_config, &mf_devices, &mf_devices_count);
	mf_config->Release();

	if (mf_devices_count > 0)
	{
        if (dev_name == NULL)
        {
            hr = mf_devices[0]->ActivateObject(IID_PPV_ARGS(&source));
        }
		else
        {
            for (DWORD i = 0; (source == NULL && i < mf_devices_count); i++)
            {
                wchar_t *name;
                mf_devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, NULL);
                if (wcscmp(name, dev_name) == 0)
                {
                    hr = mf_devices[i]->ActivateObject(IID_PPV_ARGS(&source));
                }
                CoTaskMemFree(name);
            }
        }
	}

	for (DWORD i = 0; i < mf_devices_count; i++)
	{
		mf_devices[i]->Release();
	}
	CoTaskMemFree(mf_devices);

    if (source == NULL) { return; }
    
    IMFSourceReader *reader = NULL;
    hr = MFCreateSourceReaderFromMediaSource(source, NULL, &reader);

    DWORD select_stream_index = (DWORD)-1;
    IMFMediaType *select_media_type = NULL;

    UINT32 max_sample_rate = 0;
    for (DWORD stream_index = 0;; stream_index++)
    {
        for (DWORD type_index = 0;;type_index++)
        {
            IMFMediaType *media_type = NULL;
            hr = reader->GetNativeMediaType(stream_index, type_index, &media_type);
            if (hr != S_OK) { break; }

            GUID maj_type;
            media_type->GetMajorType(&maj_type);
            if (maj_type == MFMediaType_Audio)
            {
                GUID sub_type;
                media_type->GetGUID(MF_MT_SUBTYPE, &sub_type);
                if (sub_type == MFAudioFormat_Float || sub_type == MFAudioFormat_PCM)
                {
                    UINT32 sample_rate = 0;
                    media_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sample_rate);
                    if (sample_rate > max_sample_rate)
                    {
                        media_type->AddRef();
                        SafeRelease(select_media_type);
                        select_stream_index = stream_index;
                        select_media_type = media_type;
                    }
                }
            }

            media_type->Release();
        }

        if (hr == MF_E_INVALIDSTREAMNUMBER) { break; }
    }

    if (select_media_type == NULL)
    {
        printf("no pcm media stream available\n");
        reader->Release();
        return;
    }

    UINT32 channels = 0;
    select_media_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
    UINT32 samples_rate = 0;
    select_media_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &samples_rate);

    UINT32 sample_bits = 16;
    select_media_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, sample_bits);
    select_media_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    UINT32 block_align = channels * (sample_bits >> 3);
    select_media_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, block_align);
    UINT32 avg_bytes_per_sec = samples_rate * block_align;
    select_media_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, avg_bytes_per_sec);
    hr = reader->SetCurrentMediaType(select_stream_index, NULL, select_media_type);
    select_media_type->Release();

    source_ = source;
    reader_ = reader;
    audio_stream_index_ = select_stream_index;
    sample_rate_ = samples_rate;
    channels_ = channels;

    return;
}

int AudioCapSourceMF::getSpec(DWORD *sample_rate, DWORD *channels)
{
    if (reader_ == NULL) return 0;

    *sample_rate = sample_rate_;
    *channels = channels_;

    return 1;
}

void AudioCapSourceMF::cap_routine()
{
    while (worker_run_)
    {
		DWORD streamIndex, flags;
		LONGLONG llTimeStamp;
		IMFSample *sample;
		HRESULT hr = reader_->ReadSample(audio_stream_index_, 0, &streamIndex, &flags, &llTimeStamp, &sample);
		if (hr == S_OK)
		{
			if (sample) 
			{
                DWORD data_len = 0;
                sample->GetTotalLength(&data_len);
                memset(audio_frame_, 0, sizeof(audio_frame_));

                int8_t *ptr = (int8_t*)audio_frame_;

                DWORD buf_count = 0;
                sample->GetBufferCount(&buf_count);
                for (DWORD i = 0; i < buf_count; ++i)
                {
                    IMFMediaBuffer *buf = NULL;
                    sample->GetBufferByIndex(i, &buf);
                    if (buf)
                    {
                        BYTE *data = NULL;
                        DWORD data_len = 0;
                        buf->Lock(&data, NULL, &data_len);
                        memcpy(ptr, data, data_len);
                        ptr += data_len;
                        buf->Unlock();
                        buf->Release();
                    }
                }

                sink_(audio_frame_, (data_len>>1), (llTimeStamp / 10000));
				sample->Release();
			}
		}
    }

    return;
}

#ifdef UT_AUDIO_CAP_SOURCE

#include <stdio.h>

int main(int argc, char *argv[])
{
    CoInitializeEx(0, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    FILE *fp = fopen(argv[1], "wb");
    AudioCapSourceMF cap([&fp](int16_t* sample, int sample_count, int64_t pts){
      #if 0
        printf("Audio Frame, sample_count: %d, pts: %" PRId64 "ms\n", sample_count, pts);
      #endif

        fwrite(sample, sample_count, sizeof(int16_t), fp);
    }, NULL);

    DWORD sample_rate, channels;
    if (cap.getSpec(&sample_rate, &channels))
    {
        printf("audio output spec: sample_rate: %u, channels: %u\n", sample_rate, channels);
    }

    cap.start();

    getchar();

    cap.stop();

    fclose(fp);

    MFShutdown();
    CoUninitialize();

    return 0;
}

#endif /* AUDIO_CAP_SOURCE_UT */

