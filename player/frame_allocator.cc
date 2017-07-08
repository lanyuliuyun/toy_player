
#include "frame_allocator.h"

FrameAllocator::FrameAllocator(int count, int max_frame_len)
{
	max_frame_len_ = max_frame_len;
    init(count, max_frame_len);
    InitializeCriticalSectionAndSpinCount(&frames_lock_, 3000);
}

FrameAllocator::~FrameAllocator(void)
{
    for (Frame* frame: allocated_frames_)
    {
        delete frame;
    }

    DeleteCriticalSection(&frames_lock_);
    return;
}

Frame* FrameAllocator::alloc(void)
{
    Frame *frame = NULL;

    EnterCriticalSection(&frames_lock_);
    if (!free_frames_.empty())
    {
        frame = free_frames_.front();
        free_frames_.pop_front();
    }
    else
    {
        frame = new Frame(this, max_frame_len_);
        allocated_frames_.push_back(frame);
    }
    LeaveCriticalSection(&frames_lock_);
    frame->frame_len = 0;

    return frame;
}

void FrameAllocator::free(Frame* frame)
{
    EnterCriticalSection(&frames_lock_);
    free_frames_.push_back(frame);
    LeaveCriticalSection(&frames_lock_);
    return;
}

int FrameAllocator::init(int count, int max_frame_len)
{
    while (count > 0)
    {
        count--;

        Frame* frame = new Frame(this, max_frame_len);
        allocated_frames_.push_back(frame);
        free_frames_.push_back(frame);
    }

    return 0;
}
