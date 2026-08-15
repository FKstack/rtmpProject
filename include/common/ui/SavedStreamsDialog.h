#pragma once

#include <QDialog>
#include <QSet>

#include "profiles/SavedStreamProfile.h"

class QListWidget;
class QPushButton;

class SavedStreamsDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit SavedStreamsDialog(QWidget *parent = nullptr);
    void setProfiles(const QList<SavedStreamProfile> &profiles);
    void setActiveProfileIds(const QSet<QString> &ids);

signals:
    void addRequested();
    void editRequested(const QString &profileId);
    void deleteRequested(const QString &profileId);
    void connectRequested(const QString &profileId);
    void disconnectRequested(const QString &profileId);

private:
    [[nodiscard]] QString selectedId() const;
    void updateButtons();

    QListWidget *list_ = nullptr;
    QPushButton *edit_ = nullptr;
    QPushButton *remove_ = nullptr;
    QPushButton *connect_ = nullptr;
    QPushButton *disconnect_ = nullptr;
    QSet<QString> activeIds_;
};
