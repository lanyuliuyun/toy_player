
#include "image_allocator.h"

ImageAllocator::ImageAllocator(int count, int w, int h)
{
    init(count, w, h);
    InitializeCriticalSectionAndSpinCount(&images_lock_, 3000);

    return;
}

ImageAllocator::~ImageAllocator(void)
{
    for (Image* image: allocated_images_)
    {
        delete image;
    }

    DeleteCriticalSection(&images_lock_);

    return;
}

int ImageAllocator::init(int count, int w, int h)
{
    while (count > 0)
    {
        count--;

        Image* image = new Image(this, w, h);
        allocated_images_.push_back(image);
        free_images_.push_back(image);
    }

    return 0;
}

Image* ImageAllocator::alloc(void)
{
    Image *image = NULL;

    EnterCriticalSection(&images_lock_);
    if (!free_images_.empty())
    {
        image = free_images_.front();
        free_images_.pop_front();
    }
    else
    {
        image = new Image(this, 640, 480);
        allocated_images_.push_back(image);
    }
    LeaveCriticalSection(&images_lock_);

    return image;
}

void ImageAllocator::free(Image* image)
{
    EnterCriticalSection(&images_lock_);
    free_images_.push_back(image);
    LeaveCriticalSection(&images_lock_);

    return;
}
