
#ifndef IMAGE_ALLOCATOR_H
#define IMAGE_ALLOCATOR_H

#include "encode.h"
#include <Windows.h>

#include <list>
using std::list;

class Image;

class ImageAllocator
{
  public:
    ImageAllocator(int count, int w, int h);
    ~ImageAllocator(void);

    Image* alloc(void);
    void free(Image* image);

  private:
    int init(int count, int w, int h);

  private:
    list<Image*> allocated_images_;
    list<Image*> free_images_;
    CRITICAL_SECTION images_lock_;
};

class Image
{
  public:
    Image(ImageAllocator* allocator, int w, int h) : allocator(allocator)
    {
        image = i420_image_alloc(w, h);
    }

    ~Image()
    {
        i420_image_free(image);
    }

    void release(void)
    {
        allocator->free(this);
    }

	ImageAllocator* allocator;
    i420_image_t *image;
};

#endif // !IMAGE_ALLOCATOR_H
