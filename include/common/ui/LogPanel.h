#pragma once

#include <QVector>
#include <QWidget>

#include "logging/UserMessageTypes.h"

class QPushButton;
class QTextEdit;
class QToolButton;

/**
 * @brief 面向普通用户显示有界事件消息，不承担技术日志展示职责。
 */
class LogPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit LogPanel(QWidget *parent = nullptr);

    [[nodiscard]] int entryCount() const noexcept;
    [[nodiscard]] bool isAutoScrollPaused() const noexcept;
    [[nodiscard]] QTextEdit *textEdit() const noexcept;

public slots:
    void appendMessage(const UserMessage &message);
    void clearEntries();

private:
    static constexpr int kMaximumEntries = 5'000;

    [[nodiscard]] QString formatMessage(const UserMessage &message) const;
    void appendVisibleMessage(const UserMessage &message);

    QVector<UserMessage> entries_;
    QToolButton *pauseButton_ = nullptr;
    QPushButton *clearButton_ = nullptr;
    QTextEdit *textEdit_ = nullptr;
};
