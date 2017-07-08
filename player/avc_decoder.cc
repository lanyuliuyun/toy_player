
#include "avc_decoder.h"

#include "frame_allocator.h"
#include "image_allocator.h"

extern "C" {
  #include <libswscale/swscale.h>
}

#include <algorithm>

AvcDecoder::AvcDecoder(ImageAllocator* allocator, ImageSink image_sink)
    : allocator_(allocator)
    , image_sink_(image_sink)
    , codec_ctx_(NULL)
    , av_frame_(NULL)
    , scaler_(NULL)
{
    init();

    return;
}

AvcDecoder::~AvcDecoder()
{
    avcodec_close(codec_ctx_);
    av_frame_free(&av_frame_);
    if (NULL != scaler_)
    {
        sws_freeContext(scaler_);
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
    run_ = false;
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
    while(run_)
    {
        list<Frame*> frames;
        {
            EnterCriticalSection(&frames_lock_);
            while (frames_to_decode_.empty())
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

    return;
}

int AvcDecoder::init(void)
{
    AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    codec_ctx_ = avcodec_alloc_context3(codec);

    if (avcodec_open2(codec_ctx_, codec, NULL) != 0)
    {
        return -1;
    }

    av_frame_ = av_frame_alloc();

    InitializeCriticalSectionAndSpinCount(&frames_lock_, NULL);
    InitializeConditionVariable(&frames_wait_);

    return 0;
}

int AvcDecoder::decode(Frame* frame)
{
    int ret = 0;

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = frame->data;
    packet.size = frame->frame_len;

    if (avcodec_send_packet(codec_ctx_, &packet) != 0)
    {
        ret = -1;
    }

    while (avcodec_receive_frame(codec_ctx_, av_frame_) == 0)
    {
        Image* image = allocator_->alloc();
        do_scale(av_frame_, image->image);

        image_sink_(image);
    }

    return ret;
}

void AvcDecoder::do_scale(AVFrame* av_frame, i420_image_t *i420_image)
{
    if (NULL == scaler_)
    {
        scaler_ = sws_getContext(av_frame->width, av_frame->height, (AVPixelFormat)av_frame->format,
            i420_image->width, i420_image->height, AV_PIX_FMT_YUV420P, 0, NULL, NULL, NULL);
    }

    uint8_t* dst_slice[3];
    int dst_stride[3];

    dst_slice[0] = (uint8_t*)i420_image->y_ptr;
    dst_slice[1] = (uint8_t*)i420_image->u_ptr;
    dst_slice[2] = (uint8_t*)i420_image->v_ptr;
    dst_stride[0] = i420_image->width;
    dst_stride[1] = i420_image->width>>1;
    dst_stride[2] = i420_image->width>>1;

    sws_scale(scaler_, av_frame->data, av_frame->linesize, 0, i420_image->height, dst_slice, dst_stride);

    return;
}

unsigned __stdcall AvcDecoder::thread_entry(void* arg)
{
    AvcDecoder* thiz = (AvcDecoder*)arg;
    thiz->decode_routine();
    return 0;
}
