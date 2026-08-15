#include "app/StyleLoader.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

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

QPalette darkControlRoomPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#0B1118")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#E8F3FA")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#0A1016")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#131C25")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#182430")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#E8F3FA")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#D8E6F3")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#182430")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#E8F3FA")));
    palette.setColor(QPalette::BrightText, QColor(QStringLiteral("#FFFFFF")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#20B8F0")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#04131C")));
    palette.setColor(QPalette::Link, QColor(QStringLiteral("#4CCBFF")));
    palette.setColor(QPalette::LinkVisited, QColor(QStringLiteral("#9E8CFF")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#6F8599")));
    palette.setColor(QPalette::Light, QColor(QStringLiteral("#34495D")));
    palette.setColor(QPalette::Midlight, QColor(QStringLiteral("#2A3A4A")));
    palette.setColor(QPalette::Mid, QColor(QStringLiteral("#22303E")));
    palette.setColor(QPalette::Dark, QColor(QStringLiteral("#070B10")));
    palette.setColor(QPalette::Shadow, QColor(QStringLiteral("#030609")));

    palette.setColor(
        QPalette::Disabled,
        QPalette::WindowText,
        QColor(QStringLiteral("#66798A"))
    );
    palette.setColor(
        QPalette::Disabled,
        QPalette::Text,
        QColor(QStringLiteral("#66798A"))
    );
    palette.setColor(
        QPalette::Disabled,
        QPalette::ButtonText,
        QColor(QStringLiteral("#66798A"))
    );
    palette.setColor(
        QPalette::Disabled,
        QPalette::Button,
        QColor(QStringLiteral("#111923"))
    );
    palette.setColor(
        QPalette::Disabled,
        QPalette::Highlight,
        QColor(QStringLiteral("#294354"))
    );
    return palette;
}

void applyDarkControlRoomTheme(QApplication &application,
                               const QString &styleSheet)
{
    if (QStyle *fusionStyle = QStyleFactory::create(QStringLiteral("Fusion"));
        fusionStyle != nullptr) {
        application.setStyle(fusionStyle);
        application.setProperty(
            "rtmpMonitorBaseStyle",
            QStringLiteral("Fusion")
        );
    }
    application.setPalette(darkControlRoomPalette());
    application.setStyleSheet(styleSheet);
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
            applyDarkControlRoomTheme(application, styleSheet);
            result.applied = true;
            result.source = StyleSource::ExternalFile;
            result.resolvedPath = externalStylePath;
            return result;
        }

    }

    const QString resourceStylePath = QStringLiteral(":/styles/%1").arg(options.styleFileName);
    if (readStyleSheet(resourceStylePath, &styleSheet, &errorMessage)) {
        applyDarkControlRoomTheme(application, styleSheet);
        result.applied = true;
        result.source = StyleSource::QtResource;
        result.resolvedPath = resourceStylePath;
        return result;
    }

    result.errorMessage = QStringLiteral("无法读取内置 QSS：%1。原因：%2")
                              .arg(resourceStylePath, errorMessage);
    return result;
}
