
#include "image_allocator.h"
#include "frame_allocator.h"

#include "render.h"
#include "avc_decoder.h"
#include "avc_frame_generator.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <functional>
using namespace std;

int main(int argc, char *argv[])
{
	avcodec_register_all();

    ImageAllocator image_allocator(30, 640, 480);
    FrameAllocator frame_allocator(30, 51200);

    Render render;
    AvcDecoder avc_decoder(&image_allocator, bind(&Render::submit, &render, placeholders::_1));
    AvcFrameGenerator avc_generator(&frame_allocator, bind(&AvcDecoder::submit, &avc_decoder, placeholders::_1));

    render.start();
    avc_decoder.start();
    avc_generator.start();

    render.run();

    avc_generator.stop();
    avc_decoder.stop();
    render.stop();

    return 0;
}

