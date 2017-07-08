
#include "render.h"
#include "image_allocator.h"

extern "C" {
  #include <libswscale/swscale.h>
}

Render::Render(void)
{
    init();
}

Render::~Render()
{
    DeleteCriticalSection(&images_lock_);

    SDL_DestroyWindow(win_);
    SDL_Quit();

    sws_freeContext(scaler_);
}


int Render::start(void)
{
    run_ = true;
    thread_ = _beginthreadex(NULL, 0, Render::thread_entry, this, 0, NULL);

    return 0;
}

void Render::stop(void)
{
	EnterCriticalSection(&images_lock_);
    run_ = false;
	WakeConditionVariable(&images_wait_);
	LeaveCriticalSection(&images_lock_);
    WaitForSingleObject((HANDLE)thread_, INFINITE);
    return;
}

void Render::run(void)
{
    for (;;)
    {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 500))
        {
            if (event.type == SDL_QUIT)
            {
                break;
            }
        }
    }

    return;
}

void Render::submit(Image* image)
{
    EnterCriticalSection(&images_lock_);
    images_to_render_.push_back(image);
    WakeConditionVariable(&images_wait_);
    LeaveCriticalSection(&images_lock_);
    return;
}

void Render::render_routine(void)
{
    while(run_)
    {
        list<Image*> images;
        {
            EnterCriticalSection(&images_lock_);
            while (images_to_render_.empty() && run_)
            {
                SleepConditionVariableCS(&images_wait_, &images_lock_, INFINITE);
            }
            images_to_render_.swap(images);
            LeaveCriticalSection(&images_lock_);
        }

        for (Image* image: images)
        {
            render(image);
            image->release();
        }
        images.clear();
    }

    return;
}

int Render::init(void)
{
    SDL_Init(SDL_INIT_VIDEO);
    win_ = SDL_CreateWindow("Render Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, 0);
    surface_ = SDL_GetWindowSurface(win_);

    InitializeCriticalSectionAndSpinCount(&images_lock_, NULL);
    InitializeConditionVariable(&images_wait_);

    scaler_ = sws_getContext(640, 480, AV_PIX_FMT_YUV420P, 640, 480, AV_PIX_FMT_BGRA, 0, NULL, NULL, NULL);

    return 0;
}

void Render::render(Image* image)
{
    SDL_LockSurface(surface_);

    const uint8_t* src_slice[3] = {(uint8_t*)image->image->y_ptr, (uint8_t*)image->image->u_ptr, (uint8_t*)image->image->v_ptr};
    int src_stride[3] = {image->image->width, image->image->width>>1, image->image->width>>1};

    uint8_t* dst_slice[1] = {(uint8_t*)surface_->pixels};
    int dst_stride[1] = {surface_->w * 4};

    sws_scale(scaler_, src_slice, src_stride, 0, image->image->height, dst_slice, dst_stride);

    SDL_UnlockSurface(surface_);
    SDL_UpdateWindowSurface(win_);

    return;
}

unsigned __stdcall Render::thread_entry(void* arg)
{
    Render* thiz = (Render*)arg;
    thiz->render_routine();
    return 0;
}
