#include "app/SavedStreamController.h"

#include <QMessageBox>

#include <utility>

#include "app/StreamConnectionController.h"
#include "ui/MainWindow.h"
#include "ui/SavedStreamEditorDialog.h"
#include "ui/SavedStreamsDialog.h"

SavedStreamController::SavedStreamController(
    MainWindow *mainWindow,
    StreamConnectionController *connections,
    SavedStreamRepository repository,
    QObject *parent)
    : QObject(parent), mainWindow_(mainWindow), connections_(connections),
      repository_(std::move(repository))
{
    Q_ASSERT(mainWindow_ != nullptr);
    Q_ASSERT(connections_ != nullptr);
    connect(connections_, &StreamConnectionController::connectionRemoved,
            this, [this](StreamId streamId, const QString &) {
                for (auto it = activeStreams_.begin(); it != activeStreams_.end();) {
                    if (it.value() == streamId) it = activeStreams_.erase(it);
                    else ++it;
                }
                refreshDialog();
            });
}

bool SavedStreamController::load(QString *error)
{
    const SavedStreamLoadResult result = repository_.load();
    if (!result.ok()) {
        if (error != nullptr) *error = result.error;
        profiles_.clear();
        return false;
    }
    profiles_ = result.profiles;
    if (error != nullptr) error->clear();
    return true;
}

void SavedStreamController::autoConnect()
{
    for (const SavedStreamProfile &profile : std::as_const(profiles_)) {
        if (profile.autoConnect) connectProfile(profile.profileId);
    }
}

void SavedStreamController::showDialog()
{
    if (dialog_ == nullptr) {
        dialog_ = new SavedStreamsDialog(mainWindow_);
        connect(dialog_, &SavedStreamsDialog::addRequested,
                this, &SavedStreamController::addProfile);
        connect(dialog_, &SavedStreamsDialog::editRequested,
                this, &SavedStreamController::editProfile);
        connect(dialog_, &SavedStreamsDialog::deleteRequested,
                this, &SavedStreamController::deleteProfile);
        connect(dialog_, &SavedStreamsDialog::connectRequested,
                this, &SavedStreamController::connectProfile);
        connect(dialog_, &SavedStreamsDialog::disconnectRequested,
                this, &SavedStreamController::disconnectProfile);
    }
    refreshDialog();
    dialog_->show();
    dialog_->raise();
    dialog_->activateWindow();
}

SavedStreamProfile *SavedStreamController::find(const QString &profileId)
{
    for (SavedStreamProfile &profile : profiles_)
        if (profile.profileId == profileId) return &profile;
    return nullptr;
}

const SavedStreamProfile *SavedStreamController::find(const QString &profileId) const
{
    for (const SavedStreamProfile &profile : profiles_)
        if (profile.profileId == profileId) return &profile;
    return nullptr;
}

bool SavedStreamController::persist()
{
    QString error;
    if (repository_.save(profiles_, &error)) return true;
    QMessageBox::warning(mainWindow_, tr("保存失败"), error);
    return false;
}

void SavedStreamController::refreshDialog()
{
    if (dialog_ == nullptr) return;
    dialog_->setActiveProfileIds(QSet<QString>(activeStreams_.keyBegin(),
                                               activeStreams_.keyEnd()));
    dialog_->setProfiles(profiles_);
}

void SavedStreamController::addProfile()
{
    SavedStreamEditorDialog editor(mainWindow_);
    if (editor.exec() != QDialog::Accepted) return;
    profiles_.push_back(editor.profile());
    if (!persist()) profiles_.removeLast();
    refreshDialog();
}

void SavedStreamController::editProfile(const QString &profileId)
{
    SavedStreamProfile *profile = find(profileId);
    if (profile == nullptr) return;
    if (activeStreams_.contains(profileId)) {
        QMessageBox::information(mainWindow_, tr("请先断开"),
            tr("已连接的保存项需要先断开，才能编辑名称或地址。"));
        return;
    }
    SavedStreamEditorDialog editor(mainWindow_);
    editor.setProfile(*profile);
    if (editor.exec() != QDialog::Accepted) return;
    const SavedStreamProfile previous = *profile;
    *profile = editor.profile();
    if (!persist()) *profile = previous;
    refreshDialog();
}

void SavedStreamController::deleteProfile(const QString &profileId)
{
    for (int index = 0; index < profiles_.size(); ++index) {
        if (profiles_.at(index).profileId != profileId) continue;
        const SavedStreamProfile removed = profiles_.takeAt(index);
        if (!persist()) profiles_.insert(index, removed);
        refreshDialog();
        return;
    }
}

void SavedStreamController::connectProfile(const QString &profileId)
{
    const SavedStreamProfile *profile = find(profileId);
    if (profile == nullptr) return;
    const StreamId streamId = connections_->addConnection(
        profile->displayName, profile->streamUrl, true, true);
    if (streamId != kInvalidStreamId) activeStreams_.insert(profileId, streamId);
    refreshDialog();
}

void SavedStreamController::disconnectProfile(const QString &profileId)
{
    const StreamId streamId = activeStreams_.value(profileId, kInvalidStreamId);
    if (streamId != kInvalidStreamId && connections_->removeConnection(streamId, true))
        activeStreams_.remove(profileId);
    refreshDialog();
}
