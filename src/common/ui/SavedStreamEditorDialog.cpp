#include "ui/SavedStreamEditorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

#include "profiles/SavedStreamRepository.h"

SavedStreamEditorDialog::SavedStreamEditorDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("savedStreamEditorDialog"));
    setWindowTitle(tr("推流档案"));
    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("savedStreamNameEdit"));
    name_->setMaxLength(64);
    url_ = new QLineEdit(this);
    url_->setObjectName(QStringLiteral("savedStreamUrlEdit"));
    automatic_ = new QCheckBox(tr("启动时自动连接"), this);
    automatic_->setChecked(true);
    form->addRow(tr("名称"), name_);
    form->addRow(tr("RTMP URL"), url_);
    form->addRow(QString(), automatic_);
    root->addLayout(form);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    if (QPushButton *save = buttons->button(QDialogButtonBox::Save);
        save != nullptr) {
        save->setObjectName(QStringLiteral("saveStreamProfileButton"));
        save->setProperty("styleRole", QStringLiteral("primary"));
        save->setDefault(true);
    }
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QString error;
        if (!SavedStreamRepository::validate({profile()}, &error)) {
            QMessageBox::warning(this, tr("无法保存"), error);
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SavedStreamEditorDialog::setProfile(const SavedStreamProfile &profile)
{
    profileId_ = profile.profileId;
    name_->setText(profile.displayName);
    url_->setText(profile.streamUrl);
    automatic_->setChecked(profile.autoConnect);
}

SavedStreamProfile SavedStreamEditorDialog::profile() const
{
    return {
        profileId_.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces) : profileId_,
        name_->text().trimmed(), url_->text().trimmed(), automatic_->isChecked()
    };
}
