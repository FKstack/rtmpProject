#include "ui/MqttSettingsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

MqttSettingsDialog::MqttSettingsDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("mqttSettingsDialog"));
    setWindowTitle(tr("MQTT 设置"));
    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    enabled_ = new QCheckBox(tr("启动时连接"), this);
    enabled_->setObjectName(QStringLiteral("mqttEnabledCheckBox"));
    broker_ = new QLineEdit(this);
    broker_->setObjectName(QStringLiteral("mqttBrokerEdit"));
    topic_ = new QLineEdit(this);
    topic_->setObjectName(QStringLiteral("mqttTopicEdit"));
    form->addRow(tr("状态"), enabled_);
    form->addRow(tr("Broker"), broker_);
    form->addRow(tr("Topic"), topic_);
    root->addLayout(form);
    auto *test = new QPushButton(tr("测试连接"), this);
    test->setObjectName(QStringLiteral("testMqttConnectionButton"));
    root->addWidget(test);
    testResult_ = new QLabel(this);
    testResult_->setObjectName(QStringLiteral("mqttTestResult"));
    testResult_->setProperty("severity", QStringLiteral("neutral"));
    testResult_->setWordWrap(true);
    root->addWidget(testResult_);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    if (QPushButton *save = buttons->button(QDialogButtonBox::Save);
        save != nullptr) {
        save->setObjectName(QStringLiteral("saveMqttSettingsButton"));
        save->setProperty("styleRole", QStringLiteral("primary"));
        save->setDefault(true);
    }
    root->addWidget(buttons);
    connect(test, &QPushButton::clicked, this,
            [this] { emit testRequested(options()); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void MqttSettingsDialog::setOptions(const MqttConnectionOptions &options)
{
    enabled_->setChecked(options.enabled);
    broker_->setText(options.brokerUrl);
    topic_->setText(options.topic);
}

MqttConnectionOptions MqttSettingsDialog::options() const
{
    MqttConnectionOptions result;
    result.enabled = enabled_->isChecked();
    result.brokerUrl = broker_->text().trimmed();
    result.topic = topic_->text().trimmed();
    return result;
}

void MqttSettingsDialog::setTestResult(const QString &text, bool error)
{
    testResult_->setText(text);
    testResult_->setProperty(
        "severity",
        error ? QStringLiteral("error") : QStringLiteral("success")
    );
    testResult_->style()->unpolish(testResult_);
    testResult_->style()->polish(testResult_);
}
