
#ifndef AVC_DECODER_H
#define AVC_DECODER_H

#include <mftransform.h>

#include <functional>
#include <list>
using std::function;
using std::list;

class Image;
class ImageAllocator;
typedef function<void(Image* image)> ImageSink;

class Frame;

class AvcDecoder
{
public:
	AvcDecoder(ImageAllocator* allocator, ImageSink image_sink);
	~AvcDecoder();

	int start(void);
	void stop(void);

	void submit(Frame* frame);

	void decode_routine(void);

private:
	int init(void);
	int decode(Frame* frame);

	int check_frame(uint8_t* frame, int frame_len);

	IMFSample* alloc_output_buffer(int width, int height);
	void free_output_buffer(IMFSample* sample);

	static unsigned __stdcall thread_entry(void* arg);

private:
	ImageAllocator* allocator_;
	ImageSink image_sink_;

	IMFTransform* video_decoder_;

	/* �������з������Ľ��
	 */
	bool got_first_idr_frame_;

	/* ��һ�δ�SPS�з����õ���ͼ���� */
	int es_stream_width_;
	int es_stream_height_;

	DWORD input_stream_id_;
	bool mft_hold_input_buffer_;
	DWORD input_buffer_alignment_;

	DWORD output_stream_id_;
	/* �Ƿ���MFT������buffer��������client���� */
	bool need_client_allocate_output_buffer_;
	DWORD output_buffer_alignment_;

	list<Frame*> frames_to_decode_;
	CRITICAL_SECTION frames_lock_;
	CONDITION_VARIABLE frames_wait_;
	uintptr_t thread_;
	bool run_;
};

#endif // !AVC_DECODER_H
