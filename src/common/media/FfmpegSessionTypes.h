#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
}

enum class FfmpegTrackKind {
    Video,
    Audio,
};

/** Shared only by the implementation-private FFmpeg input/decode sessions. */
struct FfmpegCodecConfiguration
{
    AVCodecParameters *parameters = avcodec_parameters_alloc();
    AVRational timeBase {0, 1};
    FfmpegTrackKind kind = FfmpegTrackKind::Video;
    int streamIndex = -1;

    ~FfmpegCodecConfiguration()
    {
        avcodec_parameters_free(&parameters);
    }
};
