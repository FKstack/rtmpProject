#include "ui/SavedStreamsDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

SavedStreamsDialog::SavedStreamsDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("savedStreamsDialog"));
    setWindowTitle(tr("保存的推流"));
    resize(680, 360);
    auto *root = new QVBoxLayout(this);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("savedStreamsList"));
    root->addWidget(list_);
    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(tr("新增"), this);
    edit_ = new QPushButton(tr("编辑"), this);
    remove_ = new QPushButton(tr("删除"), this);
    connect_ = new QPushButton(tr("连接"), this);
    disconnect_ = new QPushButton(tr("断开"), this);
    add->setObjectName(QStringLiteral("addSavedStreamButton"));
    edit_->setObjectName(QStringLiteral("editSavedStreamButton"));
    remove_->setObjectName(QStringLiteral("removeSavedStreamButton"));
    connect_->setObjectName(QStringLiteral("connectSavedStreamButton"));
    disconnect_->setObjectName(QStringLiteral("disconnectSavedStreamButton"));
    remove_->setProperty("styleRole", QStringLiteral("danger"));
    connect_->setProperty("styleRole", QStringLiteral("primary"));
    for (QPushButton *button : {add, edit_, remove_, connect_, disconnect_})
        button->setMinimumHeight(40);
    for (QPushButton *button : {add, edit_, remove_, connect_, disconnect_})
        actions->addWidget(button);
    root->addLayout(actions);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    root->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(add, &QPushButton::clicked, this, &SavedStreamsDialog::addRequested);
    connect(edit_, &QPushButton::clicked, this,
            [this] { emit editRequested(selectedId()); });
    connect(remove_, &QPushButton::clicked, this,
            [this] { emit deleteRequested(selectedId()); });
    connect(connect_, &QPushButton::clicked, this,
            [this] { emit connectRequested(selectedId()); });
    connect(disconnect_, &QPushButton::clicked, this,
            [this] { emit disconnectRequested(selectedId()); });
    connect(list_, &QListWidget::itemSelectionChanged,
            this, &SavedStreamsDialog::updateButtons);
    updateButtons();
}

void SavedStreamsDialog::setProfiles(const QList<SavedStreamProfile> &profiles)
{
    const QString selected = selectedId();
    list_->clear();
    for (const SavedStreamProfile &profile : profiles) {
        const QString active = activeIds_.contains(profile.profileId) ? tr(" · 已连接") : QString();
        const QString automatic = profile.autoConnect ? tr(" · 自动") : QString();
        auto *item = new QListWidgetItem(
            QStringLiteral("%1%2%3\n%4")
                .arg(profile.displayName, automatic, active, profile.streamUrl), list_);
        item->setData(Qt::UserRole, profile.profileId);
        if (profile.profileId == selected) item->setSelected(true);
    }
    updateButtons();
}

void SavedStreamsDialog::setActiveProfileIds(const QSet<QString> &ids)
{
    activeIds_ = ids;
    updateButtons();
}

QString SavedStreamsDialog::selectedId() const
{
    const QListWidgetItem *item = list_ != nullptr ? list_->currentItem() : nullptr;
    return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
}

void SavedStreamsDialog::updateButtons()
{
    const QString id = selectedId();
    const bool selected = !id.isEmpty();
    edit_->setEnabled(selected);
    remove_->setEnabled(selected);
    connect_->setEnabled(selected && !activeIds_.contains(id));
    disconnect_->setEnabled(selected && activeIds_.contains(id));
}
