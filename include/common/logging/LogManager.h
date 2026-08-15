#pragma once

#include <QObject>
#include <QString>

#include <memory>

#include "logging/LogTypes.h"

class LogManager final : public QObject
{
    Q_OBJECT

public:
    explicit LogManager(QObject *parent = nullptr);
    ~LogManager() override;

    LogManager(const LogManager &) = delete;
    LogManager &operator=(const LogManager &) = delete;

    bool initialize(LoggingOptions options = {});
    void shutdown();
    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] QString systemLogFilePath() const;
    [[nodiscard]] QString auditLogFilePath() const;

    void logSystem(
        LogLevel level,
        const QString &module,
        const QString &eventName,
        const QString &message,
        const QJsonObject &fields = {},
        const LogContext &context = {},
        bool aggregateRepeats = false
    );
    void logAudit(const AuditRecord &record);

    [[nodiscard]] static QString sanitizeUrl(const QString &url);
    [[nodiscard]] static QString levelName(LogLevel level);
    [[nodiscard]] static QString auditActionName(AuditAction action);
    [[nodiscard]] static QString auditResultName(AuditResult result);
    [[nodiscard]] static bool parseLevel(
        const QString &text,
        LogLevel *level
    );

signals:
    void systemFileError(const QString &message);
    void auditFileError(const QString &message);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
