
#include "audio_cap_source_mf.h"
#include "util.hpp"

#include <mfapi.h>

AudioCapSourceMF::AudioCapSourceMF(const AudioFrameSink sink) : AudioCapSourceMF(sink, NULL)
{
    return;
}

AudioCapSourceMF::AudioCapSourceMF(const AudioFrameSink sink, const wchar_t *dev_name)
    : cap_worker_()
    , worker_run_(false)
    , sink_(sink)

    , source_(NULL)
    , reader_(NULL)
    , audio_stream_index_((DWORD)-1)
{
    setup(dev_name);
    return;
}

AudioCapSourceMF::~AudioCapSourceMF()
{
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

    IMFMediaType *media_type = NULL;
	DWORD stream_index = 0;
	do
	{
		hr = reader->GetCurrentMediaType(stream_index, &media_type);
		if (hr == S_OK)
		{
			GUID major_type;
			media_type->GetMajorType(&major_type);
			if (major_type == MFMediaType_Audio)
			{
                media_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                media_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1);
                media_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
                media_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 2);
                media_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                hr = reader->SetCurrentMediaType(stream_index, NULL, media_type);
                if (hr == S_OK)
                {
                    audio_stream_index_ = stream_index;
                    break;
                }
			}

			media_type->Release();
			stream_index++;
		}
	} while (hr == S_OK);

    if (media_type != NULL)
    {
        media_type->Release();
        source_ = source;
        reader_ = reader;
    }
    else
    {
        source->Release();
        reader->Release();
    }

    return;
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
                sink_(sample);
				sample->Release();
			}
		}
    }

    return;
}

#ifdef AUDIO_CAP_SOURCE_UT

#include <stdio.h>

int wmain(int argc, wchar_t *argv[])
{
    CoInitializeEx(0, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    AudioCapSourceMF cap([](IMFSample* sample){
        LONGLONG duration;
        sample->GetSampleDuration(&duration);
        
        DWORD flags = 0;
        sample->GetSampleFlags(&flags);
        
        LONGLONG pts = 0;
        sample->GetSampleTime(&pts);
        
        DWORD data_len = 0;
        sample->GetTotalLength(&data_len);

        wprintf(L"Audio Sample, duration: %lldus, flags: 0x%X, PTS: %lld, data_len: %u\n", duration, flags, pts, data_len);
    });

    cap.start();

    getchar();

    cap.stop();

    MFShutdown();
    CoUninitialize();

    return 0;
}

#endif /* AUDIO_CAP_SOURCE_UT */

