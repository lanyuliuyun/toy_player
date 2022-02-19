
#ifndef RENDER_D2D_H
#define RENDER_D2D_H

#include <Windows.h>
#include <d2d1.h>

#include <list>
using std::list;

class Image;

class Render
{
  public:
    Render(int width, int height);
    ~Render();
    
    static void d2d_init(void);
    static void d2d_uninit(void);

    int start(void);
    void stop(void);

    void run(void);

    void submit(Image* image);

  private:
    int init(void);

    void render(Image* image);
    static LRESULT CALLBACK wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

  private:
    static ID2D1Factory* d2d_factory;

    int width_;
    int height_;
    HWND hwnd_;
    ID2D1HwndRenderTarget* render_target_;
	BYTE* bitmap_buffer_;
    
    list<Image*> images_to_render_;
    CRITICAL_SECTION images_lock_;

  private:
    Render(const Render& rhs);
    const Render& operator==(const Render* rhs);
};

#endif // !RENDER_D2D_H
