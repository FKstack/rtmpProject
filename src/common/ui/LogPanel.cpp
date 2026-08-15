#include "ui/LogPanel.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QPalette>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QColor colorForKind(UserMessageKind kind, const QPalette &palette)
{
    switch (kind) {
    case UserMessageKind::Information:
        return palette.color(QPalette::Text);
    case UserMessageKind::Success:
        return QColor(QStringLiteral("#3DDC97"));
    case UserMessageKind::Warning:
        return QColor(QStringLiteral("#F6C85F"));
    case UserMessageKind::Error:
        return QColor(QStringLiteral("#FF5D6C"));
    }
    return palette.color(QPalette::Text);
}

} // namespace

LogPanel::LogPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("logPanel"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(4);

    auto *controlsLayout = new QHBoxLayout;
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->addStretch(1);

    pauseButton_ = new QToolButton(this);
    pauseButton_->setObjectName(QStringLiteral("pauseLogScrollButton"));
    pauseButton_->setText(tr("暂停滚动"));
    pauseButton_->setCheckable(true);
    controlsLayout->addWidget(pauseButton_);

    clearButton_ = new QPushButton(tr("清空显示"), this);
    clearButton_->setObjectName(QStringLiteral("clearLogButton"));
    controlsLayout->addWidget(clearButton_);
    rootLayout->addLayout(controlsLayout);

    textEdit_ = new QTextEdit(this);
    textEdit_->setObjectName(QStringLiteral("logTextEdit"));
    textEdit_->setReadOnly(true);
    textEdit_->setAcceptRichText(false);
    textEdit_->setLineWrapMode(QTextEdit::NoWrap);
    textEdit_->document()->setMaximumBlockCount(kMaximumEntries);
    rootLayout->addWidget(textEdit_, 1);

    connect(clearButton_, &QPushButton::clicked,
            this, &LogPanel::clearEntries);
}

int LogPanel::entryCount() const noexcept
{
    return entries_.size();
}

bool LogPanel::isAutoScrollPaused() const noexcept
{
    return pauseButton_->isChecked();
}

QTextEdit *LogPanel::textEdit() const noexcept
{
    return textEdit_;
}

void LogPanel::appendMessage(const UserMessage &message)
{
    if (entries_.size() >= kMaximumEntries) {
        entries_.removeFirst();
    }
    entries_.append(message);
    appendVisibleMessage(message);
}

void LogPanel::clearEntries()
{
    entries_.clear();
    textEdit_->clear();
}

QString LogPanel::formatMessage(const UserMessage &message) const
{
    return QStringLiteral("[%1] %2")
        .arg(
            message.timestampUtc.toLocalTime().toString(
                QStringLiteral("HH:mm:ss")
            ),
            message.text
        );
}

void LogPanel::appendVisibleMessage(const UserMessage &message)
{
    QTextCursor cursor(textEdit_->document());
    cursor.movePosition(QTextCursor::End);
    if (!textEdit_->document()->isEmpty()) {
        cursor.insertBlock();
    }
    QTextCharFormat format;
    format.setForeground(colorForKind(message.kind, palette()));
    cursor.insertText(formatMessage(message), format);
    if (!isAutoScrollPaused()) {
        textEdit_->verticalScrollBar()->setValue(
            textEdit_->verticalScrollBar()->maximum()
        );
    }
}
