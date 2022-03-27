
#include "encoder_mf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct avc_encoder
{
	IMFTransform *video_encoder;
    ICodecAPI *codec_api;
    DWORD input_stream_id;
    DWORD output_stream_id;
    
    int width;
    int height;
    int fps;
    int bitrate;

    int gop_size;
    int gop_frame_count;
    
    void (*on_nalu_data)(void* nalu, unsigned len, int is_last, int is_key, void* userdata);
    void *userdata;
};

avc_encoder_t* avc_encoder_new
(
    int width, int height, int fps, int bitrate,
    void (*on_nalu_data)(void* nalu, unsigned len, int is_last, int is_key,void* userdata), void *userdata
)
{
    avc_encoder_t* encoder;

    encoder = malloc(sizeof(*encoder));
    memset(encoder, 0, sizeof(*encoder));
    encoder->width = width;
    encoder->height = height;
    encoder->fps = fps;
    encoder->bitrate = bitrate;

    encoder->on_nalu_data = on_nalu_data;
    encoder->userdata = userdata;

    encoder->gop_size = 25;
    encoder->gop_frame_count = 0;
    
    return encoder;
}


int avc_encoder_init(avc_encoder_t* encoder)
{
    int ret = -1;
	HRESULT result;

	MFT_REGISTER_TYPE_INFO input_info;
	memset(&input_info, 0, sizeof(input_info));
	input_info.guidMajorType = MFMediaType_Video;
	input_info.guidSubtype = MFVideoFormat_NV12; // MFVideoFormat_I420;

	MFT_REGISTER_TYPE_INFO output_info;
	memset(&output_info, 0, sizeof(output_info));
	output_info.guidMajorType = MFMediaType_Video;
	output_info.guidSubtype = MFVideoFormat_H264;//MFVideoFormat_WVC1;//MFVideoFormat_HEVC; //MFVideoFormat_H264; //;MFVideoFormat_VP80

	IMFTransform *video_encoder = NULL;
    ICodecAPI *codec_api = NULL;

	IMFActivate **mft_activates = NULL;
	UINT32 mft_actives_count = 0;
	result = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, 0, &input_info, &output_info, &mft_activates, &mft_actives_count);
	if (result == S_OK && mft_actives_count > 0)
	{
		printf("%u encoder actives was returned\n", mft_actives_count);

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
			printf("encoder, input: %u-%u, output: %u-%u, input_stream_count: %u, output_stream_count: %u\n", input_min, input_max, output_min, output_max, input_stream_count, output_stream_count);

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
			IMFMediaType* output_media_type = NULL;
			video_encoder->GetOutputAvailableType(output_stream_id, 0, &output_media_type);

			output_media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			output_media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);

			output_media_type->SetUINT32(MF_MT_AVG_BITRATE, 512000);

			UINT64 frame_rate_num = 25;
			UINT64 frame_rate_den = 1;
			UINT64 frame_rate = frame_rate_num << 32 | frame_rate_den;
			output_media_type->SetUINT64(MF_MT_FRAME_RATE, frame_rate);

			UINT64 width = 640;
			UINT64 height = 480;
			UINT64 width_height = width <<32 | 480;
			output_media_type->SetUINT64(MF_MT_FRAME_SIZE, width_height);

			output_media_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
			output_media_type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);

			result = video_encoder->SetOutputType(output_stream_id, output_media_type, 0);
			if (result != S_OK)
			{
				printf("failed to set output type\n");
				break;
			}

			IMFMediaType* input_media_type = NULL;
			video_encoder->GetInputAvailableType(input_stream_id, 0, &input_media_type);

			//input_media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			//input_media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_I420);

			result = video_encoder->SetInputType(input_stream_id, input_media_type, 0);
			if (result != S_OK)
			{
				printf("failed to set input type\n");
				break;
			}

			result = video_encoder->QueryInterface(&codec_api);
			VARIANT vValue;

			vValue.uintVal = eAVEncCommonRateControlMode_LowDelayVBR;
			codec_api->SetValue(&CODECAPI_AVEncCommonRateControlMode, &vValue);

		#if 1
			vValue.ulVal= eAVEncAdaptiveMode_Resolution | eAVEncAdaptiveMode_FrameRate;
			codec_api->SetValue(&CODECAPI_AVEncVideoForceSourceScanType, &vValue);
		#endif

			vValue.uintVal = 25;
			codec_api->SetValue(&CODECAPI_AVEncMPVGOPSize, &vValue);

			vValue.uintVal = 0;
			codec_api->SetValue(&CODECAPI_AVEncMPVDefaultBPictureCount, &vValue);

		#if 0
			vValue.ulVal = 4;
			codec_api->SetValue(&CODECAPI_AVEncNumWorkerThreads, &vValue);
		#endif

		#if 0
			vValue.uintVal = 0;
			codec_api->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &vValue);
		#endif

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

			puts("setup video decoder OK");
            
            encoder->input_stream_id = input_stream_id;
            encoder->output_stream_id = output_stream_id;
            encoder->video_encoder = video_encoder;
            encoder->codec_api = codec_api;

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

int avc_encoder_encode(avc_encoder_t* encoder, i420_image_t* image, int pts, int keyframe)
{
    int ret = -1;
    HRESULT result;
    
    IMFTransform *video_encoder = encoder->video_encoder;

    if (image != NULL)
    {
        IMFMediaBuffer* buffer = NULL;
        result = MFCreateMemoryBuffer(image->size, &buffer);

        BYTE *data_ptr = NULL;
        DWORD max_buffer_len = 0;
        DWORD current_data_len = 0;
        result = buffer->Lock(&data_ptr, &max_buffer_len, &current_data_len);
        memcpy(data_ptr, image->data, image->size);
        buffer->SetCurrentLength(frame->frame_len);
        buffer->Unlock();

        IMFSample *sample = NULL;
        result = MFCreateSample(&sample);
        sample->AddBuffer(buffer);
        sample->AddRef();

        result = video_encoder->ProcessInput(encoder->input_stream_id, sample, 0);
        if (result == S_OK)
        {
            ret = 0;
        }
        else
        {
            printf("video_decoder->ProcessInput() failed, ret: 0x%X\n", result);
        }

        if (mft_not_hold_input_buffer_)
        {
            buffer->Release();
            sample->RemoveAllBuffers();
            sample->Release();
        }

        /********************************************************************************/

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
            output_buffer.pSample = alloc_output_buffer(es_stream_width_, es_stream_height_);
        }

        DWORD buffer_count = 0;
        DWORD i = 0;

        result = video_encoder->ProcessOutput(MFT_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER, 1, &output_buffer, &output_flags);
        if (result == S_OK)
        {
            Image* image = allocator_->alloc();
            int image_len = 0;

            result = output_buffer.pSample->GetBufferCount(&buffer_count);
            for (i = 0; i < buffer_count; ++i)
            {
                result = output_buffer.pSample->GetBufferByIndex(0, &buffer);
                result = buffer->Lock(&data_ptr, &max_buffer_len, &current_data_len);
                
                /* FIXME: 此处填充图像数据待优化 */
                memcpy((uint8_t*)image->image->data+image_len, data_ptr, current_data_len);
                image_len += current_data_len;

                buffer->Unlock();
            }

            image_sink_(image);

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
        }

        free_output_buffer(output_buffer.pSample);
    }
    else
    {
        
    }
    
    return 0;
}

void avc_encoder_destroy(avc_encoder_t* encoder)
{
    if (video_encoder != NULL)
    {
        video_encoder->Release();
    }
    if (codec_api != NULL)
    {
        codec_api->Release();
    }
    free(encoder);
    
    return;
}
