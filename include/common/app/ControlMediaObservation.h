#pragma once

#include <QtGlobal>

struct ControlMediaObservation
{
    bool playbackPlaying = false;
    qint64 presentedFrameAgeMs = -1;
};
