#pragma once

#include <QObject>
#include <QSet>
#include <QHash>

#include "media/PlaybackTypes.h"
#include "profiles/SavedStreamRepository.h"

class MainWindow;
class StreamConnectionController;
class SavedStreamsDialog;

class SavedStreamController final : public QObject
{
    Q_OBJECT
public:
    SavedStreamController(MainWindow *mainWindow,
                          StreamConnectionController *connections,
                          SavedStreamRepository repository = SavedStreamRepository(),
                          QObject *parent = nullptr);

    [[nodiscard]] bool load(QString *error = nullptr);
    void autoConnect();
    void showDialog();

private:
    SavedStreamProfile *find(const QString &profileId);
    const SavedStreamProfile *find(const QString &profileId) const;
    bool persist();
    void refreshDialog();
    void addProfile();
    void editProfile(const QString &profileId);
    void deleteProfile(const QString &profileId);
    void connectProfile(const QString &profileId);
    void disconnectProfile(const QString &profileId);

    MainWindow *mainWindow_ = nullptr;
    StreamConnectionController *connections_ = nullptr;
    SavedStreamRepository repository_;
    QList<SavedStreamProfile> profiles_;
    SavedStreamsDialog *dialog_ = nullptr;
    QHash<QString, StreamId> activeStreams_;
};
