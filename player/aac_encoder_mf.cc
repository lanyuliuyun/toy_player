
#include "aac_encoder_mf.h"

#include <mfapi.h>
#include <mferror.h>

AACEncoder::AACEncoder(int sample_bits, int sample_rate, int channels)
  : audio_encoder_(NULL)
  , input_stream_id_((DWORD)-1)
  , output_stream_id_((DWORD)-1)
  , need_client_allocate_output_buffer_(false)
  , min_output_buffer_size_(0)
  , output_buffer_(NULL)
{
    setup(sample_bits, sample_rate, channels);
    return;
}

AACEncoder::~AACEncoder()
{
	if (audio_encoder_)
	{
		audio_encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, input_stream_id_);
		audio_encoder_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
		audio_encoder_->Release();
	}

    return;
}

int AACEncoder::put(int16_t *samples, int samples_count, int64_t pts)
{
	HRESULT hr;

	IMFMediaBuffer* buffer = NULL;
	hr = MFCreateMemoryBuffer(samples_count * 2, &buffer);
	{
		BYTE *data_ptr = NULL;
		DWORD max_buffer_len = 0;
		DWORD current_data_len = 0;
		hr = buffer->Lock(&data_ptr, &max_buffer_len, &current_data_len);
		memcpy(data_ptr, samples, samples_count * 2);
		buffer->SetCurrentLength(samples_count * 2);
		buffer->Unlock();
	}

	IMFSample *sample = NULL;
	hr = MFCreateSample(&sample);
	sample->AddBuffer(buffer);
    buffer->Release();

	sample->SetSampleDuration((samples_count / 48) * 10000);
	sample->SetSampleTime(pts * 10000);

	hr = audio_encoder_->ProcessInput(input_stream_id_, sample, 0);
    sample->Release();

	return 0;
}

int AACEncoder::get(uint8_t **data)
{
	int data_size = 0;

	MFT_OUTPUT_DATA_BUFFER output_buffer;
	memset(&output_buffer, 0, sizeof(output_buffer));
	output_buffer.dwStreamID = output_stream_id_;
	output_buffer.pSample = NULL;
	output_buffer.dwStatus = 0;
	output_buffer.pEvents = NULL;
	if (need_client_allocate_output_buffer_)
	{
		/* 需要用户来分配buffer */
		output_buffer.pSample = alloc_sample(min_output_buffer_size_);
	}
	DWORD output_flags = 0;

	HRESULT hr = audio_encoder_->ProcessOutput(MFT_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER, 1, &output_buffer, &output_flags);
	if (hr == S_OK)
	{
		DWORD buffer_count = 0;
		DWORD i = 0;
		hr = output_buffer.pSample->GetBufferCount(&buffer_count);
		for (i = 0; i < buffer_count; ++i)
		{
			IMFMediaBuffer* buffer = NULL;
			BYTE *data_ptr = NULL;
			DWORD max_buffer_len = 0;
			DWORD data_len = 0;

			hr = output_buffer.pSample->GetBufferByIndex(0, &buffer);
			hr = buffer->Lock(&data_ptr, &max_buffer_len, &data_len);

			memcpy(output_buffer_+data_size, data_ptr, data_len);
			data_size += data_len;

			buffer->Unlock();
			buffer->Release();
		}
		*data = output_buffer_;

		if (output_buffer.pEvents != NULL)
		{
			IMFCollection* ret_events = output_buffer.pEvents;
		  #if 0
			DWORD event_index = 0;
			do
			{
				IMFMediaEvent* event = NULL;
				hr = ret_events->GetElement(event_index, reinterpret_cast<IUnknown**>(&event));
				if (hr != S_OK) { break; }

				event_index++;
			} while (1);
		  #endif
			ret_events->Release();
		}
	}
	free_sample(output_buffer.pSample);

	return data_size;
}

void AACEncoder::setup(int sample_bits, int sample_rate, int channels)
{
	HRESULT hr;

	MFT_REGISTER_TYPE_INFO input_info;
	memset(&input_info, 0, sizeof(input_info));
	input_info.guidMajorType = MFMediaType_Audio;
	input_info.guidSubtype = MFAudioFormat_PCM;

	MFT_REGISTER_TYPE_INFO output_info;
	memset(&output_info, 0, sizeof(output_info));
	output_info.guidMajorType = MFMediaType_Audio;
	output_info.guidSubtype = MFAudioFormat_AAC;

	IMFActivate **mft_activates = NULL;
	UINT32 mft_actives_count = 0;
	hr = MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, 0, &input_info, &output_info, &mft_activates, &mft_actives_count);
	if (hr != S_OK || mft_actives_count == 0) { return; }

	IMFTransform *audio_encoder = NULL;

	mft_activates[0]->ActivateObject(IID_PPV_ARGS(&audio_encoder));

	DWORD input_stream_id;
	DWORD output_stream_id;
	hr = audio_encoder->GetStreamIDs(1, &input_stream_id, 1, &output_stream_id);
	if (hr != S_OK && hr != E_NOTIMPL) { return; }
	if (hr == E_NOTIMPL)
	{
		input_stream_id = 0;
		output_stream_id = 0;
	}

	/**********************************************************************/

	IMFMediaType* input_media_type = NULL;
	{
		MFCreateMediaType(&input_media_type);
		input_media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		input_media_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
		input_media_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
		input_media_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
		input_media_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
	}
	hr = audio_encoder->SetInputType(input_stream_id, input_media_type, 0);
	input_media_type->Release();

	/**********************************************************************/

	IMFMediaType* output_media_type = NULL;
	{
		MFCreateMediaType(&output_media_type);
		output_media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		output_media_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
		output_media_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
		output_media_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
		output_media_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
		output_media_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 12000);
		output_media_type->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 1);
		output_media_type->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
	}
	hr = audio_encoder->SetOutputType(output_stream_id, output_media_type, 0);
	output_media_type->Release();

	/**********************************************************************/

	MFT_INPUT_STREAM_INFO input_stream_info;
	memset(&input_stream_info, 0, sizeof(input_stream_info));
	audio_encoder->GetInputStreamInfo(input_stream_id, &input_stream_info);

	MFT_OUTPUT_STREAM_INFO output_stream_info;
	memset(&output_stream_info, 0, sizeof(output_stream_info));
	audio_encoder->GetOutputStreamInfo(output_stream_id, &output_stream_info);

	min_output_buffer_size_ = output_stream_info.cbSize;
	output_buffer_ = new uint8_t[min_output_buffer_size_];

	need_client_allocate_output_buffer_ = (output_stream_info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0;

	for (UINT32 i = 0; i < mft_actives_count; ++i)
	{
		mft_activates[i]->Release();
	}
	CoTaskMemFree(mft_activates);

	audio_encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);

	audio_encoder_ = audio_encoder;

	input_stream_id_ = input_stream_id;
	output_stream_id_ = output_stream_id;

    return;
}

IMFSample* AACEncoder::alloc_sample(int size)
{
	HRESULT hr;

	IMFMediaBuffer* buffer = NULL;
	hr = MFCreateMemoryBuffer(size, &buffer);

	IMFSample *sample = NULL;
	hr = MFCreateSample(&sample);
	sample->AddBuffer(buffer);
    buffer->Release();

	return sample;
}

void AACEncoder::free_sample(IMFSample* sample)
{
	if (sample == NULL) { return; }
	sample->Release();

	return;
}

#ifdef UT_AAC_ENCODER_MF

#include <stdio.h>

const int frame_size = 48 * 20;
int16_t samples[frame_size];

int main(int argc, char *argv[])
{
	CoInitializeEx(0, COINIT_MULTITHREADED);
	MFStartup(MF_VERSION, 0);
    {
		AACEncoder encoder(16, 48000, 1);

		int64_t pts = 0;
		FILE *fp = fopen(argv[1], "rb");
		FILE* fp_aac = fopen(argv[2], "wb");
		do
		{
			int ret = fread(samples, 2, frame_size, fp);
			if (ret != frame_size) break;

			encoder.put(samples, ret, pts);

			uint8_t* aac = NULL;
			ret = encoder.get(&aac);
			if (ret > 0)
			{
				fwrite(aac, ret, 1, fp_aac);
			}

			pts += 20;
		}while(1);

		fclose(fp);
		fclose(fp_aac);
    }

	MFShutdown();
    CoUninitialize();

    printf("=== quits ===");

    return 0;
}
 
#endif