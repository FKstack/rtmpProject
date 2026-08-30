#pragma once

#include "publisher/Mp4H264PublisherSource.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rtmp_monitor::publisher {

namespace camera_detail { class CameraSourceTestAccess; }

struct CameraDeviceInfo
{
    std::uint32_t index = 0;
    std::string alias;
};

/**
 * Runtime-only Windows camera source. Device identity never leaves the
 * Media Foundation activation scope; callers receive camera-N aliases only.
 */
class CameraH264PublisherSource final
{
public:
    CameraH264PublisherSource();
    ~CameraH264PublisherSource();

    CameraH264PublisherSource(const CameraH264PublisherSource &) = delete;
    CameraH264PublisherSource &operator=(
        const CameraH264PublisherSource &
    ) = delete;

    [[nodiscard]] static PublisherSourceError listCameras(
        std::vector<CameraDeviceInfo> *devices
    ) noexcept;
    [[nodiscard]] PublisherSourceError start(
        std::uint32_t cameraIndex,
        PublisherSubmitCallback submit
    );
    [[nodiscard]] PublisherSourceError waitForCompletion(
        std::chrono::milliseconds timeout
    );
    [[nodiscard]] PublisherSourceSnapshot snapshot() const noexcept;
    void stop() noexcept;

    [[nodiscard]] static const char *errorName(
        PublisherSourceError error
    ) noexcept;

private:
    class Impl;
    explicit CameraH264PublisherSource(std::unique_ptr<Impl> impl);
    friend class camera_detail::CameraSourceTestAccess;
    std::unique_ptr<Impl> impl_;
};

} // namespace rtmp_monitor::publisher
