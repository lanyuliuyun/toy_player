
#include "image_allocator.h"

#include <stdlib.h>
#include <string.h>

nv12_image_t* nv12_image_alloc(int width, int height)
{
    int size = (width * height * 3) >> 1;
    nv12_image_t* image;

    image = (nv12_image_t*)malloc(sizeof(*image) + size);

    image->width = width;
    image->height = height;
    image->data = (unsigned char*)&image[1];
    image->size = size;

    image->y_ptr = image->data;
    image->y_stride = width;
    image->uv_ptr = image->y_ptr + width * height;
    image->uv_stride = width;

    return image;
}

void nv12_image_free(nv12_image_t* image)
{
    free(image);
    return;
}

void fill_nv12_image(nv12_image_t* image, int index)
{
    int x, y;
    unsigned char* y_ptr;
    unsigned char* uv_ptr;
    int uv_width, uv_height;

    /* Y */
    y_ptr = image->y_ptr;
    for (y = 0; y < image->height; ++y)
    {
        for (x = 0; x < image->width; ++x)
        {
            y_ptr[x] = (unsigned char)(x + y + index * 3);
        }
        y_ptr += image->y_stride;
    }

    /* UV */
    uv_ptr = image->uv_ptr;
    uv_width = image->width;
    uv_height = image->height>>1;
    for (y = 0; y < uv_height; y++)
    {
        for (x = 0; x < uv_width; x += 2)
        {
			uv_ptr[x] = (unsigned char)(128 + y + index * 2);
			uv_ptr[x+1] = (unsigned char)(64 + x + index * 5);
        }
        uv_ptr += image->uv_stride;
    }

    return;
}

ImageAllocator::ImageAllocator(int count, int w, int h)
{
    init(count, w, h);

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
    InitializeCriticalSectionAndSpinCount(&images_lock_, 3000);
    
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
