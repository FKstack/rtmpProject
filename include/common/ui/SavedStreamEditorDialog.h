#pragma once

#include <QDialog>

#include "profiles/SavedStreamProfile.h"

class QCheckBox;
class QLineEdit;

class SavedStreamEditorDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit SavedStreamEditorDialog(QWidget *parent = nullptr);
    void setProfile(const SavedStreamProfile &profile);
    [[nodiscard]] SavedStreamProfile profile() const;

private:
    QString profileId_;
    QLineEdit *name_ = nullptr;
    QLineEdit *url_ = nullptr;
    QCheckBox *automatic_ = nullptr;
};
