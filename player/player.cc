
#include "image_allocator.h"

#include "render_d2d.h"
#include "avc_decoder_mf.h"
#include "avc_frame_generator_mf.h"

#include <mfapi.h>

#include <functional>
using namespace std;

int wmain(int argc, wchar_t *argv[])
{
	CoInitialize(NULL);
	MFStartup(MF_VERSION, 0);
    
    Render::d2d_init();

    ImageAllocator image_allocator(10, 640, 480);

    Render render(640, 480);
    AvcDecoder avc_decoder(&image_allocator, bind(&Render::submit, &render, placeholders::_1));
    AvcFrameGenerator avc_generator(640, 480, bind(&AvcDecoder::submit, &avc_decoder, placeholders::_1));

    render.start();
    avc_decoder.start();
    avc_generator.start();

    render.run();

    avc_generator.stop();
    avc_decoder.stop();
    render.stop();
    
    Render::d2d_uninit();

	MFShutdown();
	CoUninitialize();

    return 0;
}

