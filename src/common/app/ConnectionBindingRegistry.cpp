#include "app/ConnectionBindingRegistry.h"

#include <algorithm>

#include "ui/VideoWidget.h"

int ConnectionBindingRegistry::size() const noexcept
{
    return static_cast<int>(bindings_.size());
}

bool ConnectionBindingRegistry::isFull() const noexcept
{
    return size() >= kMaximumBindings;
}

bool ConnectionBindingRegistry::containsNameOrUrl(
    const QString &displayName,
    const QString &url
) const
{
    return std::any_of(bindings_.begin(), bindings_.end(),
                       [&](const ConnectionBinding &binding) {
        return binding.displayName == displayName || binding.url == url;
    });
}

bool ConnectionBindingRegistry::containsCameraId(const QString &cameraId) const
{
    return !cameraId.isEmpty() &&
           std::any_of(bindings_.begin(), bindings_.end(),
                       [&](const ConnectionBinding &binding) {
        return !binding.cameraId.isEmpty() && binding.cameraId == cameraId;
    });
}

ConnectionBinding &ConnectionBindingRegistry::add(ConnectionBinding binding)
{
    bindings_.push_back(std::move(binding));
    return bindings_.back();
}

bool ConnectionBindingRegistry::remove(StreamId streamId)
{
    const auto oldSize = bindings_.size();
    bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                   [streamId](const ConnectionBinding &binding) {
        return binding.streamId == streamId;
    }), bindings_.end());
    return bindings_.size() != oldSize;
}

ConnectionBinding *ConnectionBindingRegistry::find(StreamId streamId) noexcept
{
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                 [streamId](const ConnectionBinding &binding) {
        return binding.streamId == streamId;
    });
    return it == bindings_.end() ? nullptr : &*it;
}

const ConnectionBinding *ConnectionBindingRegistry::find(StreamId streamId) const noexcept
{
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                 [streamId](const ConnectionBinding &binding) {
        return binding.streamId == streamId;
    });
    return it == bindings_.end() ? nullptr : &*it;
}

ConnectionBinding *ConnectionBindingRegistry::find(VideoWidget *videoWidget) noexcept
{
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                 [videoWidget](const ConnectionBinding &binding) {
        return binding.videoWidget == videoWidget;
    });
    return it == bindings_.end() ? nullptr : &*it;
}

StreamId ConnectionBindingRegistry::streamIdFor(const VideoWidget *videoWidget) const noexcept
{
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                 [videoWidget](const ConnectionBinding &binding) {
        return binding.videoWidget == videoWidget;
    });
    return it == bindings_.end() ? kInvalidStreamId : it->streamId;
}

int ConnectionBindingRegistry::nextAvailableCameraNumber() const
{
    for (int number = 1; number <= kMaximumBindings; ++number) {
        const QString candidate = QStringLiteral("Camera %1")
                                      .arg(number, 2, 10, QLatin1Char('0'));
        if (std::none_of(bindings_.begin(), bindings_.end(),
                         [&](const ConnectionBinding &binding) {
                return binding.displayName == candidate;
            })) return number;
    }
    return kMaximumBindings;
}

QSet<QString> ConnectionBindingRegistry::names() const
{
    QSet<QString> result;
    for (const auto &binding : bindings_) result.insert(binding.displayName);
    return result;
}

QSet<QString> ConnectionBindingRegistry::urls() const
{
    QSet<QString> result;
    for (const auto &binding : bindings_) result.insert(binding.url);
    return result;
}
