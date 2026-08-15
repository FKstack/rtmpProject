#pragma once

#include <QDialog>

#include "device_control/DeviceControlTypes.h"

class QCheckBox;
class QLineEdit;
class QLabel;

class MqttSettingsDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit MqttSettingsDialog(QWidget *parent = nullptr);
    void setOptions(const MqttConnectionOptions &options);
    [[nodiscard]] MqttConnectionOptions options() const;
    void setTestResult(const QString &text, bool error = false);

signals:
    void testRequested(const MqttConnectionOptions &options);

private:
    QCheckBox *enabled_ = nullptr;
    QLineEdit *broker_ = nullptr;
    QLineEdit *topic_ = nullptr;
    QLineEdit *statusTopic_ = nullptr;
    QLabel *testResult_ = nullptr;
};
