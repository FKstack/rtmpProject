#include "ui/ConnectionDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

ConnectionDialog::ConnectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("connectionDialog"));
    setWindowTitle(tr("添加新的连接"));
    setModal(true);
    resize(520, 180);

    auto *rootLayout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout;

    displayNameEdit_ = new QLineEdit(this);
    displayNameEdit_->setObjectName(QStringLiteral("connectionDisplayNameEdit"));
    displayNameEdit_->setMaxLength(64);

    streamUrlEdit_ = new QLineEdit(this);
    streamUrlEdit_->setObjectName(QStringLiteral("connectionUrlEdit"));
    streamUrlEdit_->setClearButtonEnabled(true);

    formLayout->addRow(tr("设备名称"), displayNameEdit_);
    formLayout->addRow(tr("RTMP URL"), streamUrlEdit_);
    rootLayout->addLayout(formLayout);

    validationLabel_ = new QLabel(this);
    validationLabel_->setObjectName(QStringLiteral("connectionValidationLabel"));
    validationLabel_->setWordWrap(true);
    rootLayout->addWidget(validationLabel_);

    buttonBox_ = new QDialogButtonBox(
        QDialogButtonBox::Cancel, Qt::Horizontal, this
    );
    QPushButton *connectButton = buttonBox_->addButton(
        tr("添加并连接"), QDialogButtonBox::AcceptRole
    );
    connectButton->setObjectName(QStringLiteral("addAndConnectButton"));
    connectButton->setProperty("styleRole", QStringLiteral("primary"));
    connectButton->setDefault(true);
    connectButton->setMinimumHeight(40);
    rootLayout->addWidget(buttonBox_);

    connect(
        displayNameEdit_, &QLineEdit::textChanged,
        this, &ConnectionDialog::validateInput
    );
    connect(
        streamUrlEdit_, &QLineEdit::textChanged,
        this, &ConnectionDialog::validateInput
    );
    connect(buttonBox_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    validateInput();
}

void ConnectionDialog::setDefaults(
    const QString &displayName,
    const QString &rtmpUrl
)
{
    displayNameEdit_->setText(displayName);
    streamUrlEdit_->setText(rtmpUrl);
    displayNameEdit_->selectAll();
    displayNameEdit_->setFocus();
    validateInput();
}

void ConnectionDialog::setExistingConnections(
    const QSet<QString> &displayNames,
    const QSet<QString> &streamUrls
)
{
    existingDisplayNames_ = displayNames;
    existingStreamUrls_ = streamUrls;
    validateInput();
}

QString ConnectionDialog::displayName() const
{
    return displayNameEdit_->text().trimmed();
}

QString ConnectionDialog::streamUrl() const
{
    return streamUrlEdit_->text().trimmed();
}

bool ConnectionDialog::isValidRtmpUrl(const QString &urlText)
{
    const QUrl url(urlText.trimmed(), QUrl::StrictMode);
    return url.isValid() &&
           url.scheme().compare(
               QStringLiteral("rtmp"), Qt::CaseInsensitive
           ) == 0 &&
           !url.host().isEmpty() && !url.path().isEmpty();
}

void ConnectionDialog::validateInput()
{
    QString error;
    const QString name = displayName();
    const QString url = streamUrl();
    if (name.isEmpty()) {
        error = tr("设备名称不能为空。");
    } else if (existingDisplayNames_.contains(name)) {
        error = tr("当前会话中已经存在同名设备。");
    } else if (!isValidRtmpUrl(url)) {
        error = tr("请输入包含主机和路径的 rtmp:// 地址。");
    } else if (existingStreamUrls_.contains(url)) {
        error = tr("当前会话中已经存在相同的 RTMP URL。");
    }

    validationLabel_->setText(error);
    validationLabel_->setVisible(!error.isEmpty());
    if (QPushButton *button =
            buttonBox_->button(QDialogButtonBox::Ok);
        button != nullptr) {
        button->setEnabled(error.isEmpty());
    } else {
        const QList<QAbstractButton *> buttons = buttonBox_->buttons();
        for (QAbstractButton *abstractButton : buttons) {
            auto *pushButton = qobject_cast<QPushButton *>(abstractButton);
            if (pushButton != nullptr &&
                buttonBox_->buttonRole(pushButton) ==
                    QDialogButtonBox::AcceptRole) {
                pushButton->setEnabled(error.isEmpty());
            }
        }
    }
}
