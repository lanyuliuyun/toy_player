
#ifndef AVC_FRAME_GENERATOR_H
#define AVC_FRAME_GENERATOR_H

#include "encode.h"

#include <process.h>
#include <functional>
using std::function;

class Frame;
class FrameAllocator;
typedef function<void(Frame* frame)> AvcFrameSink;

class AvcFrameGenerator
{
public:
    AvcFrameGenerator(FrameAllocator* frame_allocator, AvcFrameSink frame_sink);
    ~AvcFrameGenerator();

    int start(void);
    void stop(void);

    void handleNLAU(void* nalu, unsigned len, int is_last);

    void thread_routine(void);

private:
    static void on_nalu_data(void* nalu, unsigned len, int is_last, void* userdata);
    static unsigned __stdcall thread_entry(void * arg);

private:
    FrameAllocator* frame_allocator_;
    AvcFrameSink frame_sink_;

    Frame* new_frame_;

    avc_encoder_t* encoder_;
    i420_image_t *image_;
    int image_index_;

    uintptr_t thread_;
    bool run_;
};

#endif
