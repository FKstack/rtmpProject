#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include "media/PlaybackTypes.h"

/**
 * @brief Saves fullscreen framebuffers without depending on window state.
 *
 * The service owns filename generation, directory selection and atomic PNG
 * output.  UI feedback and fullscreen lifecycle remain the window's concern.
 */
class FullscreenScreenshotService final : public QObject
{
    Q_OBJECT

public:
    explicit FullscreenScreenshotService(QObject *parent = nullptr);

    void save(QImage framebuffer, const QString &deviceName, StreamId streamId,
              const QString &outputDirectory);

signals:
    void saveStarted(const QString &path, const QImage &thumbnail);
    void saveCompleted(const QString &path, const QImage &thumbnail);
    void saveFailed(const QString &path, const QString &reason,
                    const QImage &thumbnail);

private:
    [[nodiscard]] static QString resolvedDirectory(const QString &requested);
    [[nodiscard]] static QString safeBaseName(const QString &deviceName,
                                              StreamId streamId);
    [[nodiscard]] QString nextPath(const QString &directory,
                                   const QString &deviceName,
                                   StreamId streamId);

    quint64 sequence_ = 0;
};
