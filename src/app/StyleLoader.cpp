#include "app/StyleLoader.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

namespace {

/**
 * @brief 判断样式文件名是否可安全拼接到样式目录。
 *
 * 仅接受不含路径的 `.qss` 文件名，避免未来将主题名配置化后产生路径穿越。
 */
bool isSafeStyleFileName(const QString &styleFileName)
{
    const QFileInfo fileInfo(styleFileName);
    return !styleFileName.isEmpty() && fileInfo.fileName() == styleFileName &&
           styleFileName.endsWith(QStringLiteral(".qss"), Qt::CaseInsensitive);
}

/**
 * @brief 以 UTF-8 读取 QSS 文件。
 *
 * @param path 外部文件或 QRC 资源路径。
 * @param contents 成功时写入 QSS 内容。
 * @param errorMessage 失败时写入错误说明。
 * @return 成功读取时返回 true。
 */
bool readStyleSheet(const QString &path, QString *contents, QString *errorMessage)
{
    QFile styleFile(path);
    if (!styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *errorMessage = styleFile.errorString();
        return false;
    }

    *contents = QString::fromUtf8(styleFile.readAll());
    return true;
}

} // namespace

StyleLoadResult StyleLoader::applyApplicationStyle(
    QApplication &application,
    const StyleLoadOptions &options
) const
{
    StyleLoadResult result;
    if (!isSafeStyleFileName(options.styleFileName)) {
        result.errorMessage = QStringLiteral("样式文件名必须是不含路径的 .qss 文件名。");
        qWarning().noquote() << result.errorMessage;
        return result;
    }

    const QString externalStyleDirectory = options.externalStyleDirectory.isEmpty()
        ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("styles"))
        : options.externalStyleDirectory;
    const QString externalStylePath =
        QDir(externalStyleDirectory).filePath(options.styleFileName);

    QString styleSheet;
    QString errorMessage;
    QFileInfo externalStyleInfo(externalStylePath);
    if (externalStyleInfo.exists()) {
        if (readStyleSheet(externalStylePath, &styleSheet, &errorMessage)) {
            application.setStyleSheet(styleSheet);
            result.applied = true;
            result.source = StyleSource::ExternalFile;
            result.resolvedPath = externalStylePath;
            return result;
        }

        qWarning().noquote()
            << QStringLiteral("无法读取外部 QSS：%1，尝试使用内置资源。原因：%2")
                   .arg(externalStylePath, errorMessage);
    }

    const QString resourceStylePath = QStringLiteral(":/styles/%1").arg(options.styleFileName);
    if (readStyleSheet(resourceStylePath, &styleSheet, &errorMessage)) {
        application.setStyleSheet(styleSheet);
        result.applied = true;
        result.source = StyleSource::QtResource;
        result.resolvedPath = resourceStylePath;
        return result;
    }

    result.errorMessage = QStringLiteral("无法读取内置 QSS：%1。原因：%2")
                              .arg(resourceStylePath, errorMessage);
    qWarning().noquote() << result.errorMessage;
    return result;
}
