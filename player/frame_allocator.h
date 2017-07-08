
#ifndef FRAME_ALLOCATOR_H
#define FRAME_ALLOCATOR_H

#include <list>
using std::list;

#include <Windows.h>

class Frame;

class FrameAllocator
{
public:
    FrameAllocator(int count, int max_frame_len);
    ~FrameAllocator(void);

    Frame* alloc(void);
    void free(Frame* frame);

  private:
    int init(int count, int max_frame_len);

  private:
    list<Frame*> allocated_frames_;
    list<Frame*> free_frames_;
    CRITICAL_SECTION frames_lock_;
	int max_frame_len_;
};

class Frame
{
public:
    Frame(FrameAllocator* allocator, int max_frame_len): allocator(allocator), max_frame_len(max_frame_len)
    {
        data = new uint8_t[max_frame_len];
        frame_len = 0;
        return;
    }

    ~Frame(void)
    {
        delete[] data;
        return;
    }

    void release(void)
    {
        allocator->free(this);
    }

    FrameAllocator* allocator;
    uint8_t *data;
    int frame_len;
    int max_frame_len;
};

#endif // !FRAME_ALLOCATOR_H
