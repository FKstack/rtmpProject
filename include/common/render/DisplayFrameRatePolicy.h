#pragma once
#include <QString>
#include <optional>

enum class DisplayFrameRateRequest { Auto, Fps15, Fps30, Fps60 };
enum class DisplayFrameRatePlatform { Windows, LinuxArm64 };

struct DisplayFrameRateDecision
{
    DisplayFrameRateRequest request = DisplayFrameRateRequest::Auto;
    QString requestedName = QStringLiteral("auto");
    int effectiveFps = 15;
    QString fallbackReason;
};

class DisplayFrameRatePolicy final
{
public:
    [[nodiscard]] static std::optional<DisplayFrameRateRequest> parse(const QString &value);
    [[nodiscard]] static DisplayFrameRateDecision decide(
        DisplayFrameRateRequest request,
        DisplayFrameRatePlatform platform,
        int streamCount
    );
};
