
#ifndef AVC_DECODER_H
#define AVC_DECODER_H

#include "encode.h"

extern "C" {
  #include <libavcodec/avcodec.h>
}

#include <Windows.h>
#include <process.h>

#include <functional>
#include <list>
using std::function;
using std::list;

class Image;
class ImageAllocator;
typedef function<void(Image* image)> ImageSink;

class Frame;
struct SwsContext;

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
    void do_scale(AVFrame* av_frame, i420_image_t *i420_image);

    static unsigned __stdcall thread_entry(void* arg);

private:
    ImageAllocator* allocator_;
    ImageSink image_sink_;

    AVCodecContext* codec_ctx_;
    AVFrame* av_frame_;
    struct SwsContext* scaler_;

    list<Frame*> frames_to_decode_;
    CRITICAL_SECTION frames_lock_;
    CONDITION_VARIABLE frames_wait_;
    uintptr_t thread_;
    bool run_;
};

#endif