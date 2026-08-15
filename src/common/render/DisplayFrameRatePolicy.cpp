#include "render/DisplayFrameRatePolicy.h"

std::optional<DisplayFrameRateRequest> DisplayFrameRatePolicy::parse(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("auto")) return DisplayFrameRateRequest::Auto;
    if (normalized == QStringLiteral("15")) return DisplayFrameRateRequest::Fps15;
    if (normalized == QStringLiteral("30")) return DisplayFrameRateRequest::Fps30;
    if (normalized == QStringLiteral("60")) return DisplayFrameRateRequest::Fps60;
    return std::nullopt;
}

DisplayFrameRateDecision DisplayFrameRatePolicy::decide(
    DisplayFrameRateRequest request,
    DisplayFrameRatePlatform platform,
    int streamCount
)
{
    Q_UNUSED(streamCount);
    DisplayFrameRateDecision result;
    result.request = request;
    switch (request) {
    case DisplayFrameRateRequest::Fps15:
        result.requestedName = QStringLiteral("15"); result.effectiveFps = 15; break;
    case DisplayFrameRateRequest::Fps30:
        result.requestedName = QStringLiteral("30"); result.effectiveFps = 30; break;
    case DisplayFrameRateRequest::Fps60:
        result.requestedName = QStringLiteral("60"); result.effectiveFps = 60; break;
    case DisplayFrameRateRequest::Auto:
        result.requestedName = QStringLiteral("auto");
        result.effectiveFps = platform == DisplayFrameRatePlatform::Windows ? 30 : 15;
        break;
    }
    return result;
}
