
#ifndef AUDIO_CAP_SOURCE_MF_H
#define AUDIO_CAP_SOURCE_MF_H

#include <Windows.h>
#include <Mfidl.h>
#include <mfreadwrite.h>

#include <inttypes.h>
#include <functional>
#include <thread>

class VideoCapSourceMF
{
public:
    typedef std::function<void(uint8_t *frame, int frame_size, int64_t pts)> VideoFrameSink;
    enum {
        VIDEO_FORMAT_INVALID,
        VIDEO_FORMAT_YUY2,
        VIDEO_FORMAT_MJPEG,
    };

    VideoCapSourceMF(const VideoFrameSink &sink, const wchar_t *dev_name);

    ~VideoCapSourceMF();

    int getSpec(int &video_format, int &width, int &height, int &stride, float &fps);

    int start();
    void stop();

private:
    void setup(const wchar_t *dev_name);
    void cap_routine();

    std::thread cap_worker_;
    bool worker_run_;
    VideoFrameSink sink_;

    IMFMediaSource *source_;
    IMFPresentationDescriptor *desc_;
    IMFSourceReader *reader_;
    DWORD video_stream_index_;

    int video_formats_;
    int video_width_;
    int video_height_;
    int video_stride_;
    float video_fps_;

    uint8_t *video_output_;

    VideoCapSourceMF(const VideoCapSourceMF&);
    VideoCapSourceMF& operator=(const VideoCapSourceMF&);
};

#endif /* !AUDIO_CAP_SOURCE_MF_H */
