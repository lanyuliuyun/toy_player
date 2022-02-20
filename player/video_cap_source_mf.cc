
#include "video_cap_source_mf.h"
#include "util.hpp"

#include <mfapi.h>
#include <Mferror.h>

VideoCapSourceMF::VideoCapSourceMF(const VideoFrameSink &sink, const wchar_t *dev_name)
    : cap_worker_()
    , worker_run_(false)
    , sink_(sink)

    , source_(NULL)
    , reader_(NULL)
    , video_stream_index_((DWORD)-1)

    , video_formats_(VIDEO_FORMAT_INVALID)
    , video_width_(0)
    , video_height_(0)
    , video_stride_(0)
    , video_fps_(0.0)
{
    setup(dev_name);
    return;
}

VideoCapSourceMF::~VideoCapSourceMF()
{
    SafeRelease(desc_);
    SafeRelease(source_);
    SafeRelease(reader_);

    return;
}

int VideoCapSourceMF::start()
{
    HRESULT hr;
    if (source_ == NULL || reader_ == NULL)
    {
        return -1;
    }

	PROPVARIANT var;
	PropVariantInit(&var);
	var.vt = VT_EMPTY;
	hr = source_->Start(desc_, NULL, &var);
    PropVariantClear(&var);
    
    worker_run_ = true;
    cap_worker_ = std::thread(std::bind(&VideoCapSourceMF::cap_routine, this));

    return 0;
}

void VideoCapSourceMF::stop()
{
    if (worker_run_)
    {
        worker_run_ = false;
        reader_->Flush(video_stream_index_);
        cap_worker_.join();
    }

    return;
}

void VideoCapSourceMF::setup(const wchar_t *dev_name)
{
    HRESULT hr;
	IMFMediaSource *source = NULL;

	IMFAttributes *mf_config = NULL;
	hr = MFCreateAttributes(&mf_config, 1);

	hr = mf_config->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

	IMFActivate **mf_devices = NULL;
	UINT32 mf_devices_count = 0;
	hr = MFEnumDeviceSources(mf_config, &mf_devices, &mf_devices_count);
	mf_config->Release();

	if (mf_devices_count > 0)
	{
        if (dev_name == NULL)
        {
            hr = mf_devices[0]->ActivateObject(IID_PPV_ARGS(&source));
        }
		else
        {
            for (DWORD i = 0; (source == NULL && i < mf_devices_count); i++)
            {
                wchar_t *name;
                mf_devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, NULL);
                if (wcscmp(name, dev_name) == 0)
                {
                    hr = mf_devices[i]->ActivateObject(IID_PPV_ARGS(&source));
                }
                CoTaskMemFree(name);
            }
        }
	}

	for (DWORD i = 0; i < mf_devices_count; i++)
	{
		mf_devices[i]->Release();
	}
	CoTaskMemFree(mf_devices);

    if (source == NULL) { return; }
    
    IMFPresentationDescriptor *pres_desc = NULL;
    source->CreatePresentationDescriptor(&pres_desc);

    DWORD selected_stream_index = (DWORD)-1;
    IMFMediaType *selected_media_type = NULL;
    GUID selected_media_sub_type;
    UINT32 selected_vWidth, selected_vHeight, selected_vStride, selected_FRNum, selected_FRDen;

    DWORD stream_count = 0;
    pres_desc->GetStreamDescriptorCount(&stream_count);
    for (DWORD i = 0; i < stream_count; ++i)
    {
        BOOL stream_selected = FALSE;
        IMFStreamDescriptor *stream_desc = NULL;
        pres_desc->GetStreamDescriptorByIndex(i, &stream_selected, &stream_desc);

        IMFMediaTypeHandler *media_type_handler = NULL;
        stream_desc->GetMediaTypeHandler(&media_type_handler);

        DWORD media_type_count = 0;
        media_type_handler->GetMediaTypeCount(&media_type_count);
        for (DWORD j = 0; j < media_type_count; ++j)
        {
            IMFMediaType *media_type = NULL;
            media_type_handler->GetMediaTypeByIndex(j, &media_type);
            
            GUID maj_type;
            media_type->GetMajorType(&maj_type);
            if (maj_type == MFMediaType_Video)
            {
                GUID sub_type;
                media_type->GetGUID(MF_MT_SUBTYPE, &sub_type);

                if (sub_type == MFVideoFormat_YUY2 || sub_type == MFVideoFormat_MJPG)
                {
                    UINT32 vWidth, vHeight;
                    MFGetAttributeSize(media_type, MF_MT_FRAME_SIZE, &vWidth, &vHeight);

                    UINT32 vStride;
                    media_type->GetUINT32(MF_MT_DEFAULT_STRIDE, &vStride);

                    UINT32 vFRNum, vFRDen;
                    MFGetAttributeRatio(media_type, MF_MT_FRAME_RATE, &vFRNum, &vFRDen);
                    //printf("video-spec, format: %08X, size: %ux%u, stride: %u, FR: %u/%u\n", sub_type.Data1, vWidth, vHeight, vStride, vFRNum, vFRDen);

                    // NOTE: 此处按需选择所需的图像规格
                    if ((vWidth * vHeight) >= (640 * 480) && ((vFRNum / vFRDen) >= 15))
                    {
                        selected_stream_index = i;
                        selected_media_type = media_type;
                        selected_media_sub_type = sub_type;
                        selected_vWidth = vWidth;
                        selected_vHeight = vHeight;
                        selected_vStride = vStride;
                        selected_FRNum = vFRNum;
                        selected_FRDen = vFRDen;
                        break;
                    }
                }
            }
            media_type->Release();
        }

        if (selected_media_type != NULL)
        {
            media_type_handler->SetCurrentMediaType(selected_media_type);
            pres_desc->SelectStream(selected_stream_index);

            media_type_handler->Release();
            stream_desc->Release();

            break;
        }
        else
        {
            media_type_handler->Release();
            stream_desc->Release();
        }
    }

    if (selected_media_type != NULL)
    {
        source_ = source;
        desc_ = pres_desc;
        MFCreateSourceReaderFromMediaSource(source, NULL, &reader_);

        video_stream_index_ = selected_stream_index;
        video_formats_ = (selected_media_sub_type == MFVideoFormat_YUY2) ? VIDEO_FORMAT_YUY2 : VIDEO_FORMAT_MJPEG;
        video_width_ = selected_vWidth;
        video_height_ = selected_vHeight;
        video_stride_ = selected_vStride;
        video_fps_ = (1.0f * selected_FRNum) / selected_FRDen;

        video_output_ = new uint8_t[video_width_ * video_height_ * 2];
    }
    else
    {
        pres_desc->Release();
        source->Release();
    }

    return;
}

int VideoCapSourceMF::getSpec(int &video_format, int &width, int &height, int &stride, float &fps)
{
    if (reader_ == NULL) return 0;

    video_format = video_formats_;
    width = video_width_;
    height = video_height_;
    stride = video_stride_;
    fps = video_fps_;

    return 1;
}

void VideoCapSourceMF::cap_routine()
{
    while (worker_run_)
    {
		DWORD streamIndex, flags;
		LONGLONG llTimeStamp;
		IMFSample *sample;
		HRESULT hr = reader_->ReadSample(video_stream_index_, 0, &streamIndex, &flags, &llTimeStamp, &sample);
		if (hr == S_OK)
		{
			if (sample) 
			{
                DWORD data_len = 0;
                sample->GetTotalLength(&data_len);
                int frame_size = data_len;

                if (video_formats_ == VIDEO_FORMAT_MJPEG)
                {
                    uint8_t *ptr = (uint8_t*)video_output_;

                    DWORD buf_count = 0;
                    sample->GetBufferCount(&buf_count);
                    for (DWORD i = 0; i < buf_count; ++i)
                    {
                        IMFMediaBuffer *buf = NULL;
                        sample->GetBufferByIndex(i, &buf);
                        if (buf)
                        {
                            BYTE *data = NULL;
                            DWORD data_len = 0;
                            buf->Lock(&data, NULL, &data_len);

                            memcpy(ptr, data, data_len);
                            ptr += data_len;

                            buf->Unlock();
                            buf->Release();
                        }
                    }
                }
                else // if (video_formats_ == VIDEO_FORMAT_MJPEG)
                {
                    IMFMediaBuffer *buf = NULL;
                    sample->GetBufferByIndex(0, &buf);
                    IMF2DBuffer *img_buf = NULL;
                    buf->QueryInterface(&img_buf);
                    buf->Release();

                    BYTE *data_ptr = NULL;
                    LONG pitch = 0;
                    img_buf->Lock2D(&data_ptr, &pitch);
                    memcpy(video_output_, data_ptr, frame_size);
                    img_buf->Unlock2D();

                    img_buf->Release();
                }
				sample->Release();

                sink_(video_output_, frame_size, (llTimeStamp / 10000));
			}
		}
    }

    return;
}

#ifdef UT_VIDEO_CAP_SOURCE

#include <stdio.h>

int main(int argc, char *argv[])
{
    CoInitializeEx(0, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    VideoCapSourceMF *cap = new VideoCapSourceMF([](uint8_t *frame, int frame_size, int64_t pts){
        //printf("video, frame, size: %d, pts: %" PRId64 "\n", frame_size, pts);
    }, NULL);

    int format, width, height, stride; float fps;
    if (cap->getSpec(format, width, height, stride, fps))
    {
        printf("video, formats: %d, size: %dx%d, stride: %d, FR: %.2f\n", format, width, height, stride, fps);
    }

    cap->start();

    getchar();

    cap->stop();

    delete cap;

    MFShutdown();
    CoUninitialize();

    printf("=== quit ===\n");

    return 0;
}

#endif /* AUDIO_CAP_SOURCE_UT */
