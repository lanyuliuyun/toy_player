
#include "avc_frame_generator.h"

#include "frame_allocator.h"

#include <Windows.h>
#include <string.h>

AvcFrameGenerator::AvcFrameGenerator(FrameAllocator* frame_allocator, AvcFrameSink frame_sink)
    : frame_allocator_(frame_allocator)
    , frame_sink_(frame_sink)
    , new_frame_(frame_allocator->alloc())
{
    encoder_ = avc_encoder_new(640, 480, 30, 256000, AvcFrameGenerator::on_nalu_data, this);
    avc_encoder_init(encoder_);

    image_ = i420_image_alloc(640, 480);
    image_index_ = 0;

    return;
}

AvcFrameGenerator::~AvcFrameGenerator()
{
    i420_image_free(image_);
    avc_encoder_destroy(encoder_);

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

void AvcFrameGenerator::handleNLAU(void* nalu, unsigned len, int is_last)
{
    memcpy(new_frame_->data+new_frame_->frame_len, nalu, len);
    new_frame_->frame_len += len;

    if (is_last)
    {
        frame_sink_(new_frame_);
        new_frame_ = frame_allocator_->alloc();
    }

    return;
}

void AvcFrameGenerator::thread_routine(void)
{
    while (run_)
    {
        fill_image_i420(image_, image_index_);
        avc_encoder_encode(encoder_, image_, image_index_, 0);
        image_index_++;
        Sleep(33);
    }

    return;
}

void AvcFrameGenerator::on_nalu_data(void* nalu, unsigned len, int is_last, void* userdata)
{
    AvcFrameGenerator* thiz = (AvcFrameGenerator*)userdata;
    thiz->handleNLAU(nalu, len, is_last);

    return;
}

unsigned __stdcall AvcFrameGenerator::thread_entry(void * arg)
{
    AvcFrameGenerator* thiz = (AvcFrameGenerator*)arg;
    thiz->thread_routine();
    return 0;
}
