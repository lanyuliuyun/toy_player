
#include "avc_frame_generator_mf.h"

#include "frame_allocator.h"

#include <mfapi.h>
#include <codecapi.h>
#include <mferror.h>

#include <Windows.h>
#include <string.h>

AvcFrameGenerator::AvcFrameGenerator(int width, int height, AvcFrameSink frame_sink)
    : frame_allocator_(NULL)
    , image_width_(width)
    , image_height_(height)
    , frame_sink_(frame_sink)
{
    image_ = nv12_image_alloc(width, height);
    image_index_ = 0;
    
    init();

    return;
}

AvcFrameGenerator::~AvcFrameGenerator()
{
	delete frame_allocator_;
	nv12_image_free(image_);

    return;
}

int AvcFrameGenerator::start()
{
    run_ = true;
    thread_ = _beginthreadex(NULL, 0, AvcFrameGenerator::thread_entry, this, 0, NULL);

    return 0;
}

void AvcFrameGenerator::stop()
{
    run_ = false;
    WaitForSingleObject((HANDLE)thread_, INFINITE);
    return;
}

void AvcFrameGenerator::thread_routine(void)
{
    video_encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
	bool can_feed_image = true;
	HRESULT result;

    while (run_)
    {
		if (can_feed_image)
		{
			fill_nv12_image(image_, image_index_);

			image_index_++;
			result = feedImage(image_);
			if (result == MF_E_NOTACCEPTING)
			{
				can_feed_image = false;
			}
		}

		result = tryGetEncodeOutput();
		if (result == MF_E_TRANSFORM_NEED_MORE_INPUT || result == S_OK)
		{
			can_feed_image = true;
		}

        Sleep(33);
    }

    video_encoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, input_stream_id_);
	video_encoder_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);

	tryGetEncodeOutput();

    return;
}

int AvcFrameGenerator::init(void)
{
    int ret = -1;
	HRESULT result;

	MFT_REGISTER_TYPE_INFO input_info;
	memset(&input_info, 0, sizeof(input_info));
	input_info.guidMajorType = MFMediaType_Video;
	input_info.guidSubtype = MFVideoFormat_NV12;
	//input_info.guidSubtype = MFVideoFormat_I420;

	MFT_REGISTER_TYPE_INFO output_info;
	memset(&output_info, 0, sizeof(output_info));
	output_info.guidMajorType = MFMediaType_Video;
	output_info.guidSubtype = MFVideoFormat_H264;

	IMFActivate **mft_activates = NULL;
	UINT32 mft_actives_count = 0;
	result = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, 0, &input_info, &output_info, &mft_activates, &mft_actives_count);
	if (result == S_OK && mft_actives_count > 0)
	{
		printf("%u encoder actives was returned\n", mft_actives_count);
        
        IMFTransform *video_encoder = NULL;
        ICodecAPI *codec_api = NULL;

		mft_activates[0]->ActivateObject(IID_PPV_ARGS(&video_encoder));

		do
		{
			DWORD input_min;
			DWORD input_max;
			DWORD output_min;
			DWORD output_max;

			DWORD input_stream_count;
			DWORD output_stream_count;

			video_encoder->GetStreamLimits(&input_min, &input_max, &output_min, &output_max);
			video_encoder->GetStreamCount(&input_stream_count, &output_stream_count);
			printf("encoder, input: %u-%u, output: %u-%u, input_stream_count: %u, output_stream_count: %u\n", 
                input_min, input_max, output_min, output_max, input_stream_count, output_stream_count);

			/* 对于video_encoder 其实是已知input/output stream 各只有一个 */

			DWORD input_stream_id;
			DWORD output_stream_id;
			result = video_encoder->GetStreamIDs(1, &input_stream_id, 1, &output_stream_id);
			if (result != S_OK && result != E_NOTIMPL)
			{
				printf("failed to get stream id\n");
				break;
			}
			if (result == E_NOTIMPL)
			{
				input_stream_id = 0;
				output_stream_id = 0;
			}

			/**********************************************************************/

			result = video_encoder->QueryInterface(&codec_api);
			VARIANT vValue;

			vValue.uintVal = eAVEncCommonRateControlMode_LowDelayVBR;
			codec_api->SetValue(&CODECAPI_AVEncCommonRateControlMode, &vValue);

			vValue.ulVal = eAVEncAdaptiveMode_FrameRate;
			codec_api->SetValue(&CODECAPI_AVEncAdaptiveMode, &vValue);

			vValue.ulVal = 36;
			codec_api->SetValue(&CODECAPI_AVEncCommonQualityVsSpeed, &vValue);

			vValue.boolVal = VARIANT_TRUE;
			codec_api->SetValue(&CODECAPI_AVEncH264CABACEnable, &vValue);

			vValue.uintVal = 25;
			codec_api->SetValue(&CODECAPI_AVEncMPVGOPSize, &vValue);

			vValue.uintVal = 0;
			codec_api->SetValue(&CODECAPI_AVEncMPVDefaultBPictureCount, &vValue);

			UINT64 default_qp = 8;
			UINT64 i_qp = 8;
			UINT64 p_qp = 8;
			UINT64 b_qp = 8;
			vValue.ullVal = default_qp | (i_qp << 16) | (p_qp << 32) | (b_qp << 48);
			codec_api->SetValue(&CODECAPI_AVEncVideoEncodeQP, &vValue);

		#if 0
			vValue.ulVal = 4;
			codec_api->SetValue(&CODECAPI_AVEncNumWorkerThreads, &vValue);
		#endif

		#if 0
			vValue.uintVal = 0;
			codec_api->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &vValue);
		#endif

			IMFMediaType* output_media_type = NULL;
			video_encoder->GetOutputAvailableType(output_stream_id, 0, &output_media_type);

			output_media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			output_media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);

			output_media_type->SetUINT32(MF_MT_AVG_BITRATE, 512000);

			UINT64 frame_rate_num = 25;
			UINT64 frame_rate_den = 1;
			UINT64 frame_rate = frame_rate_num << 32 | frame_rate_den;
			output_media_type->SetUINT64(MF_MT_FRAME_RATE, frame_rate);

			UINT64 width = image_width_;
			UINT64 height = image_height_;
			UINT64 width_height = width <<32 | height;
			output_media_type->SetUINT64(MF_MT_FRAME_SIZE, width_height);

			output_media_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
			output_media_type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
			output_media_type->SetUINT32(MF_MT_MPEG2_LEVEL, eAVEncH264VLevel4_2);

			result = video_encoder->SetOutputType(output_stream_id, output_media_type, 0);
			if (result != S_OK)
			{
				printf("failed to set output type\n");
				break;
			}

			IMFMediaType* input_media_type = NULL;
			video_encoder->GetInputAvailableType(input_stream_id, 0, &input_media_type);

			input_media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			input_media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
			//input_media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_I420);
			input_media_type->SetUINT64(MF_MT_FRAME_SIZE, width_height);

			result = video_encoder->SetInputType(input_stream_id, input_media_type, 0);
			if (result != S_OK)
			{
				printf("failed to set input type\n");
				break;
			}

			/**********************************************************************/

			MFT_INPUT_STREAM_INFO input_stream_info;
			memset(&input_stream_info, 0, sizeof(input_stream_info));
			video_encoder->GetInputStreamInfo(input_stream_id, &input_stream_info);

			MFT_OUTPUT_STREAM_INFO output_stream_info;
			memset(&output_stream_info, 0, sizeof(output_stream_info));
			video_encoder->GetOutputStreamInfo(output_stream_id, &output_stream_info);

			/*
			Input Stream: MaxLatency: 0, Flags: 0x7, Size: 4096, MaxLookahead: 0, Alignment: 0
			Output Stream: Flags: 0x0, Size: 0, Alignment: 0
			*/

			printf("Input Stream: MaxLatency: %lld, Flags: 0x%x, Size: %u, MaxLookahead: %u, Alignment: %u\n"
				"Output Stream: Flags: 0x%x, Size: %u, Alignment: %u\n",
				input_stream_info.hnsMaxLatency, input_stream_info.dwFlags, input_stream_info.cbSize, input_stream_info.cbMaxLookahead, input_stream_info.cbAlignment,
				output_stream_info.dwFlags, output_stream_info.cbSize, output_stream_info.cbAlignment);

			min_output_buffer_size_ = output_stream_info.cbSize;
			frame_allocator_ = new FrameAllocator(10, min_output_buffer_size_);

            mft_hold_input_buffer_ = (input_stream_info.dwFlags & MFT_INPUT_STREAM_DOES_NOT_ADDREF) == 0;
            need_client_allocate_output_buffer_ = (output_stream_info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0;

            video_encoder_ = video_encoder;
            codec_api_ = codec_api;

            input_stream_id_ = input_stream_id;
            output_stream_id_ = output_stream_id;

            ret = 0;
		} while (0);
        
        if (ret != 0)
        {
            if (video_encoder != NULL)
            {
                video_encoder->Release();
            }
            if (codec_api != NULL)
            {
                codec_api->Release();
            }
        }

		for (UINT32 i = 0; i < mft_actives_count; ++i)
		{
			mft_activates[i]->Release();
		}
		CoTaskMemFree(mft_activates);
	}
	else
	{
		puts("no specified encoder was found");
	}

    return ret;
}

IMFSample* AvcFrameGenerator::alloc_output_buffer(int size)
{
	HRESULT result;

	IMFMediaBuffer* buffer = NULL;
	result = MFCreateMemoryBuffer(size, &buffer);
    
	IMFSample *sample = NULL;
	result = MFCreateSample(&sample);
	sample->AddBuffer(buffer);
    buffer->Release();

	// TODO: 使用buffer pool

	return sample;
}

void AvcFrameGenerator::free_output_buffer(IMFSample* sample)
{
	// TODO: 使用buffer pool
	if (sample == NULL)
	{
		return;
	}

	sample->Release();

	return;
}

HRESULT AvcFrameGenerator::tryGetEncodeOutput(void)
{
	HRESULT result;

	do
	{
		MFT_OUTPUT_DATA_BUFFER output_buffer;
		DWORD output_flags = 0;

		memset(&output_buffer, 0, sizeof(output_buffer));
		output_buffer.dwStreamID = output_stream_id_;
		output_buffer.pSample = NULL;
		output_buffer.dwStatus = 0;
		output_buffer.pEvents = NULL;

		if (need_client_allocate_output_buffer_)
		{
			/* 需要用户来分配buffer */
			output_buffer.pSample = alloc_output_buffer(min_output_buffer_size_);
		}

		DWORD buffer_count = 0;
		DWORD i = 0;

		result = video_encoder_->ProcessOutput(MFT_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER, 1, &output_buffer, &output_flags);
		if (result == S_OK)
		{
			Frame* frame = frame_allocator_->alloc();
			int frame_size = 0;

		  #if 0
			UINT32 picture_type = 0;
			output_buffer.pSample->GetUINT32(MFSampleExtension_VideoEncodePictureType, &picture_type);
			enum eAVEncH264PictureType e_picture_type = (enum eAVEncH264PictureType)picture_type;

			UINT64 dts_100ns = 0;
			output_buffer.pSample->GetUINT64(MFSampleExtension_DecodeTimestamp, &dts_100ns);
			UINT64 dts_ms = dts_100ns / 10000;
		  #endif

			result = output_buffer.pSample->GetBufferCount(&buffer_count);
			for (i = 0; i < buffer_count; ++i)
			{
                IMFMediaBuffer* buffer = NULL;
                BYTE *data_ptr = NULL;
                DWORD max_buffer_len = 0;
                DWORD current_data_len = 0;

				result = output_buffer.pSample->GetBufferByIndex(0, &buffer);
				result = buffer->Lock(&data_ptr, &max_buffer_len, &current_data_len);

				/* FIXME: 此处填充图像数据待优化 */
				memcpy((uint8_t*)frame->data + frame_size, data_ptr, current_data_len);
				frame_size += current_data_len;

				buffer->Unlock();
                buffer->Release();
			}
			frame->frame_len = frame_size;
			frame_sink_(frame);

			if (output_buffer.pEvents != NULL)
			{
				output_buffer.pEvents->Release();
			}

		  #if 0
			IMFCollection* ret_events = output_buffer.pEvents;
			IMFMediaEvent* event = NULL;
			DWORD event_index = 0;
			do
			{
				result = ret_events->GetElement(event_index, reinterpret_cast<IUnknown**>(&event));
				event_index++;

				if (result != S_OK)
				{
					break;
				}
			} while (1);
			ret_events->Release();
		  #endif
		}

		free_output_buffer(output_buffer.pSample);

		if (result != S_OK)
		{
			break;
		}
	} while (true);

	return result;
}

HRESULT AvcFrameGenerator::feedImage(nv12_image_t *image)
{
	int ret = -1;
	HRESULT result;

	/********************************************************************************/

	IMFMediaBuffer* buffer = NULL;
	result = MFCreateMemoryBuffer(image->size, &buffer);

	BYTE *data_ptr = NULL;
	DWORD max_buffer_len = 0;
	DWORD current_data_len = 0;
	result = buffer->Lock(&data_ptr, &max_buffer_len, &current_data_len);
    memcpy(data_ptr, image->data, image->size);
	buffer->SetCurrentLength(image->size);
	buffer->Unlock();

	IMFSample *sample = NULL;
	result = MFCreateSample(&sample);
	sample->AddBuffer(buffer);
    buffer->Release();

	FILETIME now_ft;
	GetSystemTimeAsFileTime(&now_ft);
	ULARGE_INTEGER* ularg = (ULARGE_INTEGER*)&now_ft;
	sample->SetSampleDuration(3310);
	sample->SetSampleTime(ularg->QuadPart);

	result = video_encoder_->ProcessInput(input_stream_id_, sample, 0);
    sample->Release();

	if (result == S_OK)
	{
		ret = 0;
	}
	else
	{
		printf("video_decoder->ProcessInput() failed, ret: 0x%X\n", result);
	}

    return result;
}

unsigned __stdcall AvcFrameGenerator::thread_entry(void * arg)
{
    AvcFrameGenerator* thiz = (AvcFrameGenerator*)arg;
    thiz->thread_routine();
    return 0;
}


#ifdef TEST_AVC_ENCODE

/*
1 encoder actives was returned
encoder, input: 1-1, output: 1-1, input_stream_count: 1, output_stream_count: 1
Input Stream: MaxLatency: 0, Flags: 0x0, Size: 0, MaxLookahead: 0, Alignment: 0
Output Stream: Flags: 0x0, Size: 576000, Alignment: 0
 */

#include <stdio.h>

#include <functional>
using namespace std;

FILE* frame_fp;
FILE* frame_width_size_fp;

void on_frame(Frame* frame)
{
	fwrite(&frame->frame_len, 1, sizeof(frame->frame_len), frame_width_size_fp);
	fwrite(frame->data, 1, frame->frame_len, frame_width_size_fp);

    fwrite(frame->data, 1, frame->frame_len, frame_fp);

	frame->release();

    return;
}

int wmain(int argc, wchar_t *argv[])
{
	MFStartup(MF_VERSION, 0);
    {
	frame_fp = fopen("encode.h264", "wb");
	frame_width_size_fp = fopen("encode_with_size.h264", "wb");
    
    AvcFrameSink frame_sink = on_frame;
    
    AvcFrameGenerator avc_generator(640, 480, frame_sink);
    avc_generator.start();
    
    getchar();

    avc_generator.stop();
    fclose(frame_fp);
	fclose(frame_width_size_fp);
    }

	MFShutdown();
    
    return 0;
}
 
#endif