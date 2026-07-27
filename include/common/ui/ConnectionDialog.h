#pragma once

#include <QDialog>
#include <QSet>
#include <QString>

class QDialogButtonBox;
class QLabel;
class QLineEdit;

/** @brief 收集并本地校验一路 RTMP 连接信息。 */
class ConnectionDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(QWidget *parent = nullptr);

    void setDefaults(const QString &displayName, const QString &rtmpUrl);
    void setExistingConnections(
        const QSet<QString> &displayNames,
        const QSet<QString> &streamUrls
    );

    [[nodiscard]] QString displayName() const;
    [[nodiscard]] QString streamUrl() const;

    static bool isValidRtmpUrl(const QString &urlText);

private:
    void validateInput();

    QLineEdit *displayNameEdit_ = nullptr;
    QLineEdit *streamUrlEdit_ = nullptr;
    QLabel *validationLabel_ = nullptr;
    QDialogButtonBox *buttonBox_ = nullptr;
    QSet<QString> existingDisplayNames_;
    QSet<QString> existingStreamUrls_;
};
