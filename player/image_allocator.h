
#ifndef IMAGE_ALLOCATOR_H
#define IMAGE_ALLOCATOR_H

typedef struct nv12_image
{
    int width;
    int height;
    unsigned char* data;
    int size;
    unsigned char* y_ptr;
    int y_stride;
    unsigned char* uv_ptr;
    int uv_stride;
}nv12_image_t;

extern "C" {
  nv12_image_t* nv12_image_alloc(int width, int height);
  void nv12_image_free(nv12_image_t* image);
  void fill_nv12_image(nv12_image_t* image, int index);
}

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
        image = nv12_image_alloc(w, h);
    }

    ~Image()
    {
        nv12_image_free(image);
    }

    void release(void)
    {
        allocator->free(this);
    }

    ImageAllocator* allocator;
    nv12_image_t *image;
};

#endif // !IMAGE_ALLOCATOR_H
