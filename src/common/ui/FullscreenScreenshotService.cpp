#include "ui/FullscreenScreenshotService.h"

#include <utility>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QRunnable>
#include <QStandardPaths>
#include <QThreadPool>

#include "evidence/AtomicPngWriter.h"

namespace {

QString sanitizeFilenameComponent(QString value)
{
    value = value.trimmed();
    value.replace(
        QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")),
        QStringLiteral("_")
    );
    value.remove(QRegularExpression(QStringLiteral("[ .]+$")));
    if (value.size() > 80) {
        value.truncate(80);
        value.remove(QRegularExpression(QStringLiteral("[ .]+$")));
    }

    static const QRegularExpression reservedName(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption
    );
    if (reservedName.match(value).hasMatch()) {
        value.prepend(QLatin1Char('_'));
    }
    return value;
}

} // namespace

FullscreenScreenshotService::FullscreenScreenshotService(QObject *parent)
    : QObject(parent)
{
}

void FullscreenScreenshotService::save(
    QImage framebuffer,
    const QString &deviceName,
    StreamId streamId,
    const QString &outputDirectory
)
{
    const QString directory = resolvedDirectory(outputDirectory);
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        emit saveFailed({}, tr("截图失败：无法创建截图目录"), framebuffer);
        return;
    }

    const QString path = nextPath(directory, deviceName, streamId);
    const QImage thumbnail = framebuffer.scaled(
        QSize(320, 180), Qt::KeepAspectRatio, Qt::FastTransformation
    );
    emit saveStarted(path, thumbnail);

    QPointer<FullscreenScreenshotService> service(this);
    QThreadPool::globalInstance()->start(QRunnable::create(
        [framebuffer = std::move(framebuffer), path, thumbnail, service]() mutable {
            const AtomicPngWriteResult write =
                AtomicPngWriter::write(framebuffer, path);
            const QString error = write.error;

            if (QCoreApplication *application = QCoreApplication::instance();
                application != nullptr) {
                QMetaObject::invokeMethod(
                    application,
                    [service, path, thumbnail, error] {
                        if (service == nullptr) {
                            return;
                        }
                        if (error.isEmpty()) {
                            emit service->saveCompleted(path, thumbnail);
                        } else {
                            emit service->saveFailed(
                                path, service->tr("截图失败：%1").arg(error),
                                thumbnail
                            );
                        }
                    },
                    Qt::QueuedConnection
                );
            }
        }
    ));
}

QString FullscreenScreenshotService::resolvedDirectory(const QString &requested)
{
    const QString trimmed = requested.trimmed();
    if (!trimmed.isEmpty()) {
        return QDir::cleanPath(trimmed);
    }

    QString root = QStandardPaths::writableLocation(
        QStandardPaths::PicturesLocation
    );
    if (!root.isEmpty()) {
        return QDir(root).filePath(QStringLiteral("RtmpMonitor/Screenshots"));
    }
    root = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation
    );
    return root.isEmpty()
        ? QString()
        : QDir(root).filePath(QStringLiteral("Screenshots"));
}

QString FullscreenScreenshotService::safeBaseName(
    const QString &deviceName,
    StreamId streamId
)
{
    const QString base = sanitizeFilenameComponent(deviceName);
    if (!base.isEmpty()) {
        return base;
    }
    return streamId == kInvalidStreamId
        ? QStringLiteral("camera")
        : QStringLiteral("camera-%1").arg(streamId);
}

QString FullscreenScreenshotService::nextPath(
    const QString &directory,
    const QString &deviceName,
    StreamId streamId
)
{
    const QString timestamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd-HHmmss-zzz")
    );
    const quint64 sequence = ++sequence_;
    return QDir(directory).filePath(
        QStringLiteral("%1-%2-%3.png")
            .arg(safeBaseName(deviceName, streamId), timestamp)
            .arg(sequence, 4, 10, QLatin1Char('0'))
    );
}
