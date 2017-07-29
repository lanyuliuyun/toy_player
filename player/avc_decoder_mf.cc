
#include "avc_decoder_mf.h"

#include "frame_allocator.h"
#include "image_allocator.h"

#include <mfapi.h>

#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

AvcDecoder::AvcDecoder(ImageAllocator* allocator, ImageSink image_sink)
    : allocator_(allocator)
    , image_sink_(image_sink)
	, video_decoder_(NULL)
	, got_first_idr_frame_(false)
	, es_stream_width_(-1)
	, es_stream_height_(-1)
{
	init();

	/* FIXME: 此时暂先将宽高默认为 640x480 */
	es_stream_width_ = 640;
	es_stream_height_ = 480;

	return;
}

AvcDecoder::~AvcDecoder()
{
	if (NULL != video_decoder_)
	{
		video_decoder_->Release();
	}

	DeleteCriticalSection(&frames_lock_);

	return;
}

int AvcDecoder::start(void)
{
	run_ = true;
	thread_ = _beginthreadex(NULL, 0, AvcDecoder::thread_entry, this, 0, NULL);

	return 0;
}

void AvcDecoder::stop(void)
{
	EnterCriticalSection(&frames_lock_);
	run_ = false;
	WakeConditionVariable(&frames_wait_);
	LeaveCriticalSection(&frames_lock_);

	WaitForSingleObject((HANDLE)thread_, INFINITE);
	return;
}

void AvcDecoder::submit(Frame* frame)
{
	EnterCriticalSection(&frames_lock_);
	frames_to_decode_.push_back(frame);
	WakeConditionVariable(&frames_wait_);
	LeaveCriticalSection(&frames_lock_);
	return;
}

void AvcDecoder::decode_routine(void)
{
	video_decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);

	list<Frame*> frames;

	while (run_)
	{
		{
			EnterCriticalSection(&frames_lock_);
			while (frames_to_decode_.empty() && run_)
			{
				SleepConditionVariableCS(&frames_wait_, &frames_lock_, INFINITE);
			}
			frames_to_decode_.swap(frames);
			LeaveCriticalSection(&frames_lock_);
		}

		list<Frame*>::iterator iter = frames.begin();
		list<Frame*>::iterator iter_end = frames.end();
		for (; iter != iter_end; ++iter)
		{
			Frame* frame = *iter;
			if (decode(frame) == 0)
			{
				frame->release();
			}
			else
			{
				EnterCriticalSection(&frames_lock_);
				iter_end--;
				for (; iter_end != iter; iter_end--)
				{
					frames_to_decode_.push_front(*iter_end);
				}
				frames_to_decode_.push_front(*iter);
				LeaveCriticalSection(&frames_lock_);

				break;
			}
		}
		frames.clear();
	}

	{
		EnterCriticalSection(&frames_lock_);
		frames_to_decode_.swap(frames);
		LeaveCriticalSection(&frames_lock_);

		list<Frame*>::iterator iter = frames.begin();
		list<Frame*>::iterator iter_end = frames.end();

		for (; iter != iter_end; ++iter)
		{
			Frame* frame = *iter;
			decode(frame);
			frame->release();
		}
	}

	video_decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, input_stream_id_);

	video_decoder_->Release();
	video_decoder_ = NULL;

	return;
}

int AvcDecoder::init(void)
{
	InitializeCriticalSectionAndSpinCount(&frames_lock_, 3000);
	InitializeConditionVariable(&frames_wait_);

	int ret = -1;
	HRESULT result;

	MFT_REGISTER_TYPE_INFO input_info;
	memset(&input_info, 0, sizeof(input_info));
	input_info.guidMajorType = MFMediaType_Video;
	input_info.guidSubtype = MFVideoFormat_H264;

	MFT_REGISTER_TYPE_INFO output_info;
	memset(&output_info, 0, sizeof(output_info));
	output_info.guidMajorType = MFMediaType_Video;
	//output_info.guidSubtype = MFVideoFormat_I420;
	output_info.guidSubtype = MFVideoFormat_NV12;

	IMFActivate **mft_activates = NULL;
	UINT32 mft_actives_count = 0;
	result = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, 0, &input_info, &output_info, &mft_activates, &mft_actives_count);
	if (result == S_OK && mft_actives_count > 0)
	{
		printf("%u decoder actives was returned\n", mft_actives_count);

		do
		{
			IMFTransform *video_decoder = NULL;
			result = mft_activates[0]->ActivateObject(IID_PPV_ARGS(&video_decoder));

			DWORD input_min;
			DWORD input_max;
			DWORD output_min;
			DWORD output_max;

			DWORD input_stream_count;
			DWORD output_stream_count;

			video_decoder->GetStreamLimits(&input_min, &input_max, &output_min, &output_max);
			video_decoder->GetStreamCount(&input_stream_count, &output_stream_count);
			printf("decoder, input: %u-%u, output: %u-%u, input_stream_count: %u, output_stream_count: %u\n", 
					input_min, input_max, output_min, output_max, input_stream_count, output_stream_count);

			/* 对于 video_decoder 是已知input/output stream 各只有一个 */

			DWORD input_stream_id;
			DWORD output_stream_id;
			result = video_decoder->GetStreamIDs(1, &input_stream_id, 1, &output_stream_id);
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

			IMFMediaType* input_media_type = NULL;
			video_decoder->GetInputAvailableType(input_stream_id, 0, &input_media_type);
			result = video_decoder->SetInputType(input_stream_id, input_media_type, 0);
			if (result != S_OK)
			{
				printf("failed to set input type\n");
				break;
			}

			IMFMediaType* output_media_type = NULL;
			video_decoder->GetOutputAvailableType(output_stream_id, 0, &output_media_type);
			video_decoder->SetOutputType(output_stream_id, output_media_type, 0);
			if (result != S_OK)
			{
				printf("failed to set output type\n");
				break;
			}

			MFT_INPUT_STREAM_INFO input_stream_info;
			memset(&input_stream_info, 0, sizeof(input_stream_info));
			video_decoder->GetInputStreamInfo(input_stream_id, &input_stream_info);

			MFT_OUTPUT_STREAM_INFO output_stream_info;
			memset(&output_stream_info, 0, sizeof(output_stream_info));
			video_decoder->GetOutputStreamInfo(output_stream_id, &output_stream_info);

			/*
			Input Stream: MaxLatency: 0, Flags: 0x7, Size: 4096, MaxLookahead: 0, Alignment: 0
			Output Stream: Flags: 0x0, Size: 0, Alignment: 0
			*/

			printf("Input Stream: MaxLatency: %lld, Flags: 0x%x, Size: %u, MaxLookahead: %u, Alignment: %u\n"
				"Output Stream: Flags: 0x%x, Size: %u, Alignment: %u\n",
				input_stream_info.hnsMaxLatency, input_stream_info.dwFlags, input_stream_info.cbSize, input_stream_info.cbMaxLookahead, input_stream_info.cbAlignment,
				output_stream_info.dwFlags, output_stream_info.cbSize, output_stream_info.cbAlignment);

			mft_hold_input_buffer_ = (input_stream_info.dwFlags & MFT_INPUT_STREAM_DOES_NOT_ADDREF) == 0;
			need_client_allocate_output_buffer_ = (output_stream_info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0;

			puts("setup video decoder OK");

			input_buffer_alignment_ = input_stream_info.cbAlignment;
			output_buffer_alignment_ = output_stream_info.cbAlignment;

			video_decoder_ = video_decoder;
			input_stream_id_ = input_stream_id;
			output_stream_id_ = output_stream_id;

			ret = 0;
		} while (0);

		for (UINT32 i = 0; i < mft_actives_count; ++i)
		{
			mft_activates[i]->Release();
		}
		CoTaskMemFree(mft_activates);
	}

	return ret;
}

static
void analyze_sps(const uint8_t *nalu, size_t nalu_size, int* width, int* height)
{
	// TODO: 从SPS中分析出宽高
	return;
}

enum {
	H264_NALU_TYPE_NONIDR = 1,
	H264_NALU_TYPE_IDR = 5,
	H264_NALU_TYPE_SPS = 7,
	H264_NALU_TYPE_PPS = 8,
};

int AvcDecoder::check_frame(uint8_t* frame, int frame_len)
{
	/* TODO:
	 * 1, 检查是否已经获取到SPS/PPS；若没有并且当前不是IDR帧，则丢弃之
	 * 2，若是IDR帧，分析SPS中记录的宽高
	 */

	const uint8_t *byte;
	uint32_t next4bytes;
	const uint8_t *nalu1_begin;
	const uint8_t *nalu2_begin;
	uint8_t nalu_type;

	bool first_nalu = true;
	bool got_sps_nalu = false;
	bool got_pps_nalu = false;
	int es_width = 0;
	int es_height = 0;

	/* H264白皮书 B.1.2 节对 NALU 语义结构说明如下
	 *
	 * leading_zero_8bits: 0x00 当 NALU 为字节流的第一个 NALU 时包含
	 * zero_byte: 0x00 当 NALU 为 SPS/PPS, 或为 Access Unit 的第一个 NALU 时包含
	 * start_code_prefix_one_3bytes: 0x000001, NALU 起始码前缀
	 *  < 具体的 NALU 数据 >
	 * trailing_zero_8bits: 0x00
	 *
	 * 综上述条件，可以看出具体的 NALU 数据是被 0x00000001 所分割的，或额外包含 0 字节
	 * 下述分割过程既是基于上述结构来进行的
	 */

	byte = frame;
	while ((byte + 4) < (frame + frame_len))
	{
		next4bytes = ((uint32_t)byte[0] << 24) | ((uint32_t)byte[1] << 16) | ((uint32_t)byte[2] << 8) | (uint32_t)byte[3];

		if (next4bytes != 0x00000001)
		{
			byte++;
			continue;
		}

		/* 跳过自身的 start_code_prefix_one_3bytes，以及 leading_zero_8bits 或前一个 NALU 的 trailing_zero_8bits */
		nalu1_begin = byte + 4;

		nalu2_begin = nalu1_begin + 1;		/* 跳过nalu_type字节 */
		while ((nalu2_begin + 4) < (frame + frame_len))
		{
			next4bytes = ((uint32_t)nalu2_begin[0] << 24) | ((uint32_t)nalu2_begin[1] << 16) | ((uint32_t)nalu2_begin[2] << 8) | (uint32_t)nalu2_begin[3];
			if (next4bytes == 0x00000001)
			{
				break;
			}

			nalu2_begin++;
		}
	
		nalu_type = nalu1_begin[0];
		if (nalu_type == H264_NALU_TYPE_SPS)
		{
			got_sps_nalu = 1;
			analyze_sps(nalu1_begin, (nalu2_begin - nalu1_begin), &es_width, &es_height);
		}
		else if (nalu_type == H264_NALU_TYPE_PPS)
		{
			got_pps_nalu = 1;
		}

		if (!got_first_idr_frame_)
		{
			if (got_sps_nalu && got_pps_nalu)
			{
				got_first_idr_frame_ = true;
				break;
			}
		}

		if ((nalu2_begin + 4) == (frame + frame_len))
		{
			/* nalu1_begin 指向的 NALU 已经是当前帧中的最后一个 NALU */
			break;
		}
		else /* if ((nalu2_begin + 4) < (frame + frame_len)) */
		{
			byte = nalu2_begin;
		}
	}

	if (!got_first_idr_frame_)
	{
		printf("No IDR frame arrived yet, drop frame\n");
		return -1;
	}

	if ((es_width > 0 && es_height > 0) &&
		(es_stream_width_ != es_width || es_stream_width_ != es_height))
	{
		// NOTE: 码流发生变化，重新分配output buffer ?

		es_stream_width_ = es_width;
		es_stream_height_ = es_height;
	}

	return 0;
}

IMFSample* AvcDecoder::alloc_output_buffer(int width, int height)
{
	HRESULT result;
	IMFMediaBuffer* buffer = NULL;
	IMFSample *sample = NULL;

	result = MFCreateSample(&sample);

	buffer = NULL;
	DWORD sample_buffer_size = (DWORD)(es_stream_width_ * es_stream_height_ * 1.5);
	result = MFCreateMemoryBuffer(sample_buffer_size, &buffer);
	sample->AddBuffer(buffer);

	// FIXME: 使用buffer pool

	return sample;
}

void AvcDecoder::free_output_buffer(IMFSample* sample)
{
	// FIXME: 使用buffer pool

	if (sample == NULL)
	{
		return;
	}

	HRESULT result;
	DWORD buffer_count = 0;
	DWORD i = 0;

	IMFMediaBuffer* buffer = NULL;
	buffer_count = 0;
	result = sample->GetBufferCount(&buffer_count);
	for (i = 0; i < buffer_count; ++i)
	{
		result = sample->GetBufferByIndex(i, &buffer);
		buffer->Release();
	}
	sample->Release();

	return;
}

int AvcDecoder::decode(Frame* frame)
{
	int ret = -1;
	HRESULT result;

  #if 0
	if (check_frame(frame->data, frame->frame_len) != 0)
	{
		return 0;
	}
  #endif

	/********************************************************************************/

	IMFMediaBuffer* buffer = NULL;
	result = MFCreateMemoryBuffer(frame->frame_len, &buffer);

	BYTE *data_ptr = NULL;
	DWORD max_buffer_len = 0;
	DWORD current_data_len = 0;
	result = buffer->Lock(&data_ptr, &max_buffer_len, &current_data_len);
	memcpy(data_ptr, frame->data, frame->frame_len);
	buffer->SetCurrentLength(frame->frame_len);
	buffer->Unlock();

	IMFSample *sample = NULL;
	result = MFCreateSample(&sample);
	sample->AddBuffer(buffer);

	result = video_decoder_->ProcessInput(input_stream_id_, sample, 0);
	if (result == S_OK)
	{
		ret = 0;
	}
	else
	{
		//printf("video_decoder->ProcessInput() failed, ret: 0x%X\n", result);
	}

	if (!mft_hold_input_buffer_)
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

	result = video_decoder_->ProcessOutput(MFT_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER, 1, &output_buffer, &output_flags);
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
		if (ret_events)
		{
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
	}

	free_output_buffer(output_buffer.pSample);

	return ret;
}

unsigned __stdcall AvcDecoder::thread_entry(void* arg)
{
	AvcDecoder* thiz = (AvcDecoder*)arg;
	thiz->decode_routine();
	return 0;
}

#ifdef TEST_AVC_DECODER

/*
decoder, input: 1-1, output: 1-1, input_stream_count: 1, output_stream_count: 1
Input Stream: MaxLatency: 0, Flags: 0x7, Size: 4096, MaxLookahead: 0, Alignment: 0
Output Stream: Flags: 0x7, Size: 4147200, Alignment: 0
 */

#include <stdio.h>

FILE* fp;
FILE* fp_img;

void on_img(Image* image)
{
	fwrite(image->image->data, 1, image->image->size, fp_img);

	image->release();

	return;
}

int main(int argc, char *argv[])
{
	char frame_buffer[640*48];
	int frame_buffer_len = 640 * 48;
	int frame_size = 0;

	MFStartup(MF_VERSION, 0);

	fp = fopen("encode_with_size.h264", "rb");
	fp_img = fopen("img.nv12", "wb");

	ImageAllocator image_allocator(10, 640, 480);
	FrameAllocator frame_allocator(10, frame_buffer_len);

	ImageSink img_sink = on_img;
	AvcDecoder avc_decoder(&image_allocator, img_sink);

	avc_decoder.start();

	while (!feof(fp))
	{
		Frame* frame = frame_allocator.alloc();

		fread(&frame->frame_len, 1, sizeof(frame->frame_len), fp);
		fread(frame->data, 1, frame->frame_len, fp);

		avc_decoder.submit(frame);
	}

	Sleep(40000);

	avc_decoder.stop();

	fclose(fp);
	fclose(fp_img);

	MFShutdown();

	return 0;
}

#endif