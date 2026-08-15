#include "evidence/AtomicPngWriter.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

AtomicPngWriteResult AtomicPngWriter::write(const QImage &image,
                                            const QString &absolutePath)
{
    if (image.isNull())
        return {false, 0, QStringLiteral("截图图像为空。")};
    const QFileInfo info(absolutePath);
    if (!QDir().mkpath(info.absolutePath()))
        return {false, 0, QStringLiteral("无法创建截图目录。")};
    QSaveFile output(absolutePath);
    if (!output.open(QIODevice::WriteOnly))
        return {false, 0, QStringLiteral("无法创建 PNG 文件：%1").arg(output.errorString())};
    if (!image.save(&output, "PNG")) {
        output.cancelWriting();
        return {false, 0, QStringLiteral("PNG 编码失败。")};
    }
    const qint64 size = output.pos();
    if (!output.commit())
        return {false, 0, QStringLiteral("无法原子提交 PNG 文件。")};
    return {true, size, {}};
}
