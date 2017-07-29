
#ifndef AVC_FRAME_GENERATOR_H
#define AVC_FRAME_GENERATOR_H

#include "image_allocator.h"

#include <mftransform.h>
#include <icodecapi.h>

#include <process.h>
#include <functional>
using std::function;

class Frame;
class FrameAllocator;
typedef function<void(Frame* frame)> AvcFrameSink;

class AvcFrameGenerator
{
public:
    AvcFrameGenerator(int width, int height, AvcFrameSink frame_sink);
    ~AvcFrameGenerator();

    int start(void);
    void stop(void);

    void thread_routine(void);

private:
    int init(void);
	HRESULT feedImage(nv12_image_t *image);
	HRESULT tryGetEncodeOutput(void);
    
	IMFSample* alloc_output_buffer(int size);
	void free_output_buffer(IMFSample* sample);
	    
    static unsigned __stdcall thread_entry(void * arg);

private:
    FrameAllocator* frame_allocator_;
    AvcFrameSink frame_sink_;
	DWORD min_output_buffer_size_;
    
    int image_width_;
    int image_height_;
    
    IMFTransform* video_encoder_;
    ICodecAPI *codec_api_;
    
	DWORD input_stream_id_;
	bool mft_hold_input_buffer_;

	DWORD output_stream_id_;
	/* 是否有MFT来分配buffer，而不是client分配 */
	bool need_client_allocate_output_buffer_;

	nv12_image_t *image_;
    int image_index_;

    uintptr_t thread_;
    bool run_;
};

#endif
