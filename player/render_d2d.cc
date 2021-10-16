
#include "render_d2d.h"
#include "image_allocator.h"

#include <d2d1helper.h>

#include <libyuv/convert_argb.h>
#include <stdlib.h>

ID2D1Factory* Render::d2d_factory = NULL;

Render::Render(int width, int height)
    : width_(width)
    , height_(height)
    , hwnd_(NULL)
	, render_target_(NULL)
	, bitmap_buffer_(NULL)
	, bitmap_bufer_len((width*height)<<2)
{
    init();
}

Render::~Render()
{
    if (render_target_ != NULL)
    {
        render_target_->Release();
    }
    DeleteCriticalSection(&images_lock_);

	free(bitmap_buffer_);
}

void Render::d2d_init(void)
{
	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory);
    
    return;
}


void Render::d2d_uninit(void)
{
    if (d2d_factory != NULL)
    {
        d2d_factory->Release();
        d2d_factory = NULL;
    }

    return;
}


int Render::start(void)
{
    return 0;
}

void Render::stop(void)
{
    EnterCriticalSection(&images_lock_);
    for (auto image: images_to_render_)
    {
        image->release();
    }
    images_to_render_.clear();
    LeaveCriticalSection(&images_lock_);

    return;
}

void Render::run(void)
{
	if (render_target_ == NULL)
	{
		printf("no render target available\n");
		return;
	}

    MSG msg;
    while (GetMessageW(&msg, hwnd_, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return;
}

void Render::submit(Image* image)
{
    EnterCriticalSection(&images_lock_);
    images_to_render_.push_back(image);
    LeaveCriticalSection(&images_lock_);

    InvalidateRect(hwnd_, NULL, FALSE);

    return;
}

int Render::init(void)
{
	HRESULT result;

	InitializeCriticalSectionAndSpinCount(&images_lock_, NULL);

	WNDCLASSEXW wndclsex = {
		sizeof(WNDCLASSEXW),
		CS_HREDRAW | CS_VREDRAW,
		wndproc,
		0,
		64,
		GetModuleHandleW(NULL),
		NULL,
		LoadCursor(NULL, IDC_ARROW),
		/*(HBRUSH)GetStockObject(BLACK_BRUSH), */
		NULL,
		NULL,
		L"RenderWindow",
		NULL
	};
	RegisterClassEx(&wndclsex);

    int wnd_width = width_ + GetSystemMetrics(SM_CXFRAME) * 2;
    int wnd_height = height_ + GetSystemMetrics(SM_CYFRAME) * 2 + GetSystemMetrics(SM_CYCAPTION);
	hwnd_ = CreateWindowExW(
        0,
		wndclsex.lpszClassName,
		L"Render Window",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, 
        CW_USEDEFAULT,
		wnd_width, 
        wnd_height,
		NULL, 
        NULL, 
        wndclsex.hInstance, 
        this);

	SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);

	ShowWindow(hwnd_, SW_SHOWNORMAL);
	UpdateWindow(hwnd_);

	D2D1_RENDER_TARGET_PROPERTIES render_target_property = {
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
		0.0, 0.0,
		D2D1_RENDER_TARGET_USAGE_NONE,
		D2D1_FEATURE_LEVEL_DEFAULT
	};

	D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_render_target_property = {
		hwnd_,
		D2D1::SizeU(width_, height_),
		D2D1_PRESENT_OPTIONS_NONE
	};
	result = d2d_factory->CreateHwndRenderTarget(render_target_property, hwnd_render_target_property, &render_target_);
	if (result != S_OK)
	{
		printf("failed to create a render target, result: 0x%x\n", result);
	}

	bitmap_buffer_ = (BYTE*)malloc(bitmap_bufer_len);

    return 0;
}

void Render::onRender(void)
{
	HRESULT result;

    Image* image = NULL;
    EnterCriticalSection(&images_lock_);
    if (!images_to_render_.empty())
    {
        image = images_to_render_.front();
        images_to_render_.pop_front();
    }
    LeaveCriticalSection(&images_lock_);

    if (image == NULL)
    {
        return;
    }

	libyuv::NV12ToARGB(
		image->image->y_ptr, image->image->y_stride,
		image->image->uv_ptr, image->image->uv_stride,
		bitmap_buffer_, (width_<<2),
		width_, height_
	);
	image->release();

    D2D1_BITMAP_PROPERTIES bitmap_property = {
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        0.0, 0.0
    };

    ID2D1Bitmap *d2d_bitmap = NULL;
    result = render_target_->CreateBitmap(
        D2D1::SizeU(width_, height_),
		bitmap_buffer_, (width_<<2),
        &bitmap_property, 
        &d2d_bitmap);

	D2D1_RECT_F dst_rect = { 0, 0, (float)image->image->width, (float)image->image->height };
    render_target_->BeginDraw();
    render_target_->DrawBitmap(d2d_bitmap, dst_rect);
    render_target_->EndDraw();
    d2d_bitmap->Release();

    return;
}

LRESULT CALLBACK Render::wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_PAINT:
        {
            Render* thiz = (Render*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
            thiz->onRender();
            break;
        }
        case WM_CLOSE:
        {
            PostQuitMessage(0);
			break;
        }
        default:
        {
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }
    }

    return 0;
}


#ifdef UT_D2D_RENDER

int wmain(int argc, wchar_t *argv)
{
	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	Render::d2d_init();

	ImageAllocator image_allocator(1, 640, 480);
	Render render(640, 480);

	render.start();

	Image* image = image_allocator.alloc();
	fill_nv12_image(image->image, 1);
	render.submit(image);

	render.run();

	render.stop();

	Render::d2d_uninit();
	CoUninitialize();

	return 0;
}

#endif