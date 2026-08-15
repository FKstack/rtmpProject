#include "logging/LogManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

#include "logging/SensitiveDataSanitizer.h"

namespace {

using Clock = std::chrono::steady_clock;

QJsonObject systemEntryToJson(const SystemLogEntry &entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), 1);
    object.insert(QStringLiteral("channel"), QStringLiteral("system"));
    object.insert(
        QStringLiteral("timestampUtc"),
        entry.timestampUtc.toString(Qt::ISODateWithMs)
    );
    object.insert(
        QStringLiteral("level"),
        LogManager::levelName(entry.level).toLower()
    );
    object.insert(QStringLiteral("module"), entry.module);
    object.insert(QStringLiteral("event"), entry.eventName);
    object.insert(QStringLiteral("message"), entry.message);
    object.insert(QStringLiteral("threadId"), entry.threadId);
    object.insert(QStringLiteral("repeatedCount"), entry.repeatedCount);
    if (entry.context.deviceId != 0) {
        object.insert(
            QStringLiteral("deviceId"),
            QString::number(entry.context.deviceId)
        );
    }
    if (!entry.context.deviceName.isEmpty()) {
        object.insert(
            QStringLiteral("deviceName"),
            entry.context.deviceName
        );
    }
    if (!entry.sanitizedUrl.isEmpty()) {
        object.insert(QStringLiteral("url"), entry.sanitizedUrl);
    }
    if (!entry.fields.isEmpty()) {
        object.insert(QStringLiteral("fields"), entry.fields);
    }
    return object;
}

QJsonObject auditEntryToJson(const AuditRecord &record)
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), 1);
    object.insert(QStringLiteral("channel"), QStringLiteral("audit"));
    object.insert(
        QStringLiteral("timestampUtc"),
        record.timestampUtc.toString(Qt::ISODateWithMs)
    );
    object.insert(QStringLiteral("actor"), record.actor);
    object.insert(
        QStringLiteral("action"),
        LogManager::auditActionName(record.action)
    );
    object.insert(QStringLiteral("targetType"), record.targetType);
    object.insert(QStringLiteral("targetId"), record.targetId);
    object.insert(
        QStringLiteral("result"),
        LogManager::auditResultName(record.result)
    );
    object.insert(QStringLiteral("reason"), record.reason);
    object.insert(QStringLiteral("before"), record.beforeValues);
    object.insert(QStringLiteral("after"), record.afterValues);
    object.insert(QStringLiteral("source"), record.source);
    return object;
}

QByteArray compactJson(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

struct PendingRecord
{
    QByteArray payload;
    LogLevel priority = LogLevel::Info;
};

class AsyncJsonlSink final
{
public:
    using ErrorCallback = std::function<void(const QString &)>;

    AsyncJsonlSink(
        QString directory,
        QString fileName,
        LogFileOptions options,
        bool audit,
        bool fileEnabled,
        bool consoleEnabled,
        int recoveryRetryMs,
        ErrorCallback errorCallback
    )
        : directory_(std::move(directory))
        , fileName_(std::move(fileName))
        , filePath_(QDir(directory_).filePath(fileName_))
        , options_(options)
        , audit_(audit)
        , fileEnabled_(fileEnabled)
        , consoleEnabled_(consoleEnabled)
        , recoveryRetryMs_(std::max(1'000, recoveryRetryMs))
        , errorCallback_(std::move(errorCallback))
    {
        options_.maximumFileBytes = std::max<qint64>(
            1'024, options_.maximumFileBytes
        );
        options_.retainedFileCount = std::clamp(
            options_.retainedFileCount, 1, 100
        );
        options_.retentionDays = std::clamp(
            options_.retentionDays, 1, 3'650
        );
        options_.maximumTotalBytes = std::max(
            options_.maximumTotalBytes,
            options_.maximumFileBytes
        );
        options_.queueCapacity = std::clamp(
            options_.queueCapacity, 16, 100'000
        );
    }

    ~AsyncJsonlSink()
    {
        stop(0);
    }

    bool start()
    {
        if (fileEnabled_ && !QDir().mkpath(directory_)) {
            reportError(
                QStringLiteral("无法创建日志目录：%1").arg(directory_)
            );
            return false;
        }
        if (fileEnabled_) {
            cleanupFiles();
        }
        stopping_.store(false, std::memory_order_release);
        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this] {
            run();
        });
        return true;
    }

    void stop(int timeoutMs)
    {
        if (!running_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto maximum =
                static_cast<std::size_t>(options_.queueCapacity);
            if (dropped_ > 0 && queue_.size() >= maximum) {
                const auto lowPriority = std::find_if(
                    queue_.begin(), queue_.end(),
                    [](const PendingRecord &entry) {
                        return static_cast<int>(entry.priority) <=
                               static_cast<int>(LogLevel::Info);
                    }
                );
                if (lowPriority != queue_.end()) {
                    queue_.erase(lowPriority);
                } else {
                    queue_.pop_front();
                }
            }
            appendOverflowSummaryLocked(maximum + 1);
            stopping_.store(true, std::memory_order_release);
            deadline_ = Clock::now() +
                std::chrono::milliseconds(std::max(0, timeoutMs));
        }
        condition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    void enqueue(PendingRecord record)
    {
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto maximum =
            static_cast<std::size_t>(options_.queueCapacity);
        if (queue_.size() >= maximum) {
            if (!audit_ &&
                static_cast<int>(record.priority) <=
                    static_cast<int>(LogLevel::Info)) {
                ++dropped_;
                return;
            }
            auto candidate = queue_.end();
            if (!audit_) {
                candidate = std::find_if(
                    queue_.begin(), queue_.end(),
                    [](const PendingRecord &entry) {
                        return static_cast<int>(entry.priority) <=
                               static_cast<int>(LogLevel::Info);
                    }
                );
            }
            if (candidate != queue_.end()) {
                queue_.erase(candidate);
            } else {
                queue_.pop_front();
            }
            ++dropped_;
        }
        appendOverflowSummaryLocked(maximum);
        queue_.push_back(std::move(record));
        condition_.notify_one();
    }

    [[nodiscard]] QString filePath() const
    {
        return filePath_;
    }

private:
    void appendOverflowSummaryLocked(std::size_t maximum)
    {
        if (dropped_ == 0 || queue_.size() + 1 >= maximum) {
            return;
        }
        QJsonObject summary;
        summary.insert(QStringLiteral("schemaVersion"), 1);
        summary.insert(
            QStringLiteral("channel"),
            audit_ ? QStringLiteral("audit") : QStringLiteral("system")
        );
        summary.insert(
            QStringLiteral("timestampUtc"),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        );
        summary.insert(
            audit_ ? QStringLiteral("action") : QStringLiteral("event"),
            audit_
                ? QStringLiteral("AUDIT_QUEUE_OVERFLOW")
                : QStringLiteral("queue_overflow")
        );
        summary.insert(
            QStringLiteral("droppedCount"),
            static_cast<qint64>(dropped_)
        );
        if (!audit_) {
            summary.insert(QStringLiteral("level"), QStringLiteral("warning"));
            summary.insert(QStringLiteral("module"), QStringLiteral("logging"));
            summary.insert(
                QStringLiteral("message"),
                QStringLiteral("日志队列过载，部分低优先级记录已丢弃。")
            );
        }
        queue_.push_back(
            {compactJson(summary), LogLevel::Critical}
        );
        dropped_ = 0;
    }

    void run()
    {
        while (true) {
            PendingRecord record;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] {
                    return stopping_.load(std::memory_order_acquire) ||
                           !queue_.empty();
                });
                if (queue_.empty()) {
                    if (stopping_.load(std::memory_order_acquire)) {
                        break;
                    }
                    continue;
                }
                if (stopping_.load(std::memory_order_acquire) &&
                    Clock::now() >= deadline_) {
                    queue_.clear();
                    break;
                }
                record = std::move(queue_.front());
                queue_.pop_front();
            }

            if (consoleEnabled_ && !audit_) {
                std::fwrite(
                    record.payload.constData(),
                    1,
                    static_cast<std::size_t>(record.payload.size()),
                    stderr
                );
                std::fflush(stderr);
            }
            if (!fileEnabled_) {
                continue;
            }
            const auto now = Clock::now();
            if (!operational_ && now < nextRecoveryAttempt_) {
                ++writeFailures_;
                continue;
            }
            if (writePayload(record.payload)) {
                if (!operational_) {
                    operational_ = true;
                    errorReported_ = false;
                }
            } else {
                operational_ = false;
                ++writeFailures_;
                nextRecoveryAttempt_ = now +
                    std::chrono::milliseconds(recoveryRetryMs_);
                reportError(
                    QStringLiteral("无法写入日志文件：%1").arg(filePath_)
                );
            }
        }
    }

    bool writePayload(const QByteArray &payload)
    {
        if (!rotateIfNeeded(payload.size())) {
            return false;
        }
        QFile file(filePath_);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            return false;
        }
        return file.write(payload) == payload.size() && file.flush();
    }

    bool rotateIfNeeded(qint64 additionalBytes)
    {
        QFile current(filePath_);
        if (!current.exists() ||
            current.size() + additionalBytes <=
                options_.maximumFileBytes) {
            return true;
        }
        for (int index = options_.retainedFileCount;
             index >= 1; --index) {
            const QString destination =
                filePath_ + QStringLiteral(".%1").arg(index);
            if (index == options_.retainedFileCount) {
                QFile::remove(destination);
            }
            const QString source = index == 1
                ? filePath_
                : filePath_ +
                    QStringLiteral(".%1").arg(index - 1);
            if (QFile::exists(source) &&
                !QFile::rename(source, destination)) {
                return false;
            }
        }
        cleanupFiles();
        return true;
    }

    void cleanupFiles()
    {
        QDir directory(directory_);
        const QFileInfoList files = directory.entryInfoList(
            {fileName_ + QStringLiteral("*")},
            QDir::Files,
            QDir::Time | QDir::Reversed
        );
        const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(
            -options_.retentionDays
        );
        for (const QFileInfo &file : files) {
            if (file.absoluteFilePath() != filePath_ &&
                file.lastModified().toUTC() < cutoff) {
                QFile::remove(file.absoluteFilePath());
            }
        }

        QFileInfoList remaining = directory.entryInfoList(
            {fileName_ + QStringLiteral("*")},
            QDir::Files,
            QDir::Time | QDir::Reversed
        );
        qint64 totalBytes = 0;
        for (const QFileInfo &file : remaining) {
            totalBytes += file.size();
        }
        for (const QFileInfo &file : remaining) {
            if (totalBytes <= options_.maximumTotalBytes) {
                break;
            }
            if (file.absoluteFilePath() == filePath_) {
                continue;
            }
            const qint64 size = file.size();
            if (QFile::remove(file.absoluteFilePath())) {
                totalBytes -= size;
            }
        }
    }

    void reportError(const QString &message)
    {
        if (errorReported_) {
            return;
        }
        errorReported_ = true;
        const QByteArray encoded = message.toUtf8() + '\n';
        std::fwrite(
            encoded.constData(),
            1,
            static_cast<std::size_t>(encoded.size()),
            stderr
        );
        std::fflush(stderr);
        if (errorCallback_) {
            errorCallback_(message);
        }
    }

    QString directory_;
    QString fileName_;
    QString filePath_;
    LogFileOptions options_;
    bool audit_ = false;
    bool fileEnabled_ = true;
    bool consoleEnabled_ = false;
    int recoveryRetryMs_ = 60'000;
    ErrorCallback errorCallback_;

    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<PendingRecord> queue_;
    std::thread worker_;
    std::atomic_bool running_ {false};
    std::atomic_bool stopping_ {false};
    Clock::time_point deadline_;
    Clock::time_point nextRecoveryAttempt_;
    std::uint64_t dropped_ = 0;
    std::uint64_t writeFailures_ = 0;
    bool operational_ = true;
    bool errorReported_ = false;
};

QString currentThreadId()
{
    return QStringLiteral("0x%1").arg(
        reinterpret_cast<quintptr>(QThread::currentThreadId()),
        0,
        16
    );
}

} // namespace

class LogManager::Impl final
{
public:
    struct RepeatState
    {
        qint64 lastEmittedMs = 0;
        int suppressedCount = 0;
        SystemLogEntry lastEntry;
    };

    explicit Impl(LogManager *owner)
        : owner(owner)
    {
    }

    bool accepts(LogLevel level, const QString &module) const
    {
        const auto iterator = options.moduleMinimumLevels.constFind(
            module.trimmed().toLower()
        );
        const LogLevel minimum =
            iterator == options.moduleMinimumLevels.constEnd()
                ? options.minimumLevel
                : iterator.value();
        return static_cast<int>(level) >= static_cast<int>(minimum);
    }

    void enqueueSystem(const SystemLogEntry &entry)
    {
        if (systemSink != nullptr) {
            systemSink->enqueue(
                {compactJson(systemEntryToJson(entry)), entry.level}
            );
        }
    }

    void enqueueAudit(const AuditRecord &record)
    {
        if (auditSink != nullptr) {
            auditSink->enqueue(
                {compactJson(auditEntryToJson(record)), LogLevel::Critical}
            );
        }
    }

    LogManager *owner = nullptr;
    LoggingOptions options;
    std::atomic_bool initialized {false};
    std::unique_ptr<AsyncJsonlSink> systemSink;
    std::unique_ptr<AsyncJsonlSink> auditSink;
    std::mutex repeatMutex;
    std::unordered_map<std::string, RepeatState> repeatStates;
};

LogManager::LogManager(QObject *parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>(this))
{
    qRegisterMetaType<LogLevel>();
    qRegisterMetaType<SystemLogEntry>();
    qRegisterMetaType<AuditRecord>();
}

LogManager::~LogManager()
{
    shutdown();
}

bool LogManager::initialize(LoggingOptions options)
{
    if (impl_->initialized.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }
    if (options.directoryPath.trimmed().isEmpty()) {
        options.directoryPath = QDir(
            QStandardPaths::writableLocation(
                QStandardPaths::AppLocalDataLocation
            )
        ).filePath(QStringLiteral("logs"));
    }
    options.repeatWindowMs = std::max(0, options.repeatWindowMs);
    options.shutdownFlushTimeoutMs = std::clamp(
        options.shutdownFlushTimeoutMs, 0, 30'000
    );
    impl_->options = std::move(options);

    bool ready = true;
    if (impl_->options.systemFileEnabled ||
        impl_->options.consoleEnabled) {
        impl_->systemSink = std::make_unique<AsyncJsonlSink>(
            impl_->options.directoryPath,
            QStringLiteral("system.jsonl"),
            impl_->options.systemFile,
            false,
            impl_->options.systemFileEnabled,
            impl_->options.consoleEnabled,
            impl_->options.recoveryRetryMs,
            [this](const QString &message) {
                QMetaObject::invokeMethod(
                    this,
                    [this, message] {
                        emit systemFileError(message);
                    },
                    Qt::QueuedConnection
                );
            }
        );
        ready = impl_->systemSink->start() && ready;
        if (!impl_->options.systemFileEnabled) {
            // 控制台仍由异步 sink 输出；文件路径保持不可见。
        }
    }
    if (impl_->options.auditFileEnabled) {
        impl_->auditSink = std::make_unique<AsyncJsonlSink>(
            impl_->options.directoryPath,
            QStringLiteral("audit.jsonl"),
            impl_->options.auditFile,
            true,
            true,
            false,
            impl_->options.recoveryRetryMs,
            [this](const QString &message) {
                QMetaObject::invokeMethod(
                    this,
                    [this, message] {
                        emit auditFileError(message);
                    },
                    Qt::QueuedConnection
                );
            }
        );
        ready = impl_->auditSink->start() && ready;
    }
    return ready;
}

void LogManager::shutdown()
{
    if (!impl_->initialized.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(impl_->repeatMutex);
        for (auto &[key, state] : impl_->repeatStates) {
            Q_UNUSED(key)
            if (state.suppressedCount <= 0) {
                continue;
            }
            state.lastEntry.timestampUtc = QDateTime::currentDateTimeUtc();
            state.lastEntry.repeatedCount = state.suppressedCount;
            impl_->enqueueSystem(state.lastEntry);
        }
        impl_->repeatStates.clear();
    }
    if (impl_->auditSink != nullptr) {
        impl_->auditSink->stop(
            impl_->options.shutdownFlushTimeoutMs
        );
        impl_->auditSink.reset();
    }
    if (impl_->systemSink != nullptr) {
        impl_->systemSink->stop(
            impl_->options.shutdownFlushTimeoutMs
        );
        impl_->systemSink.reset();
    }
}

bool LogManager::isInitialized() const noexcept
{
    return impl_->initialized.load(std::memory_order_acquire);
}

QString LogManager::systemLogFilePath() const
{
    return impl_->options.systemFileEnabled &&
           impl_->systemSink != nullptr
        ? impl_->systemSink->filePath()
        : QString();
}

QString LogManager::auditLogFilePath() const
{
    return impl_->options.auditFileEnabled &&
           impl_->auditSink != nullptr
        ? impl_->auditSink->filePath()
        : QString();
}

void LogManager::logSystem(
    LogLevel level,
    const QString &module,
    const QString &eventName,
    const QString &message,
    const QJsonObject &fields,
    const LogContext &context,
    bool aggregateRepeats
)
{
    if (!isInitialized() || !impl_->accepts(level, module)) {
        return;
    }
    SystemLogEntry entry;
    entry.timestampUtc = QDateTime::currentDateTimeUtc();
    entry.level = level;
    entry.module = module.trimmed();
    entry.eventName = eventName.trimmed();
    entry.context.deviceId = context.deviceId;
    entry.context.deviceName =
        SensitiveDataSanitizer::sanitizeText(context.deviceName.trimmed());
    entry.sanitizedUrl =
        SensitiveDataSanitizer::sanitizeUrl(context.url);
    entry.message = SensitiveDataSanitizer::sanitizeText(message.trimmed());
    entry.fields = SensitiveDataSanitizer::sanitizeObject(fields);
    entry.threadId = currentThreadId();

    if (aggregateRepeats && impl_->options.repeatWindowMs > 0) {
        const std::string key = QStringLiteral("%1|%2|%3|%4")
                                    .arg(entry.context.deviceId)
                                    .arg(entry.module)
                                    .arg(entry.eventName)
                                    .arg(
                                        entry.message +
                                        QString::fromUtf8(
                                            QJsonDocument(entry.fields)
                                                .toJson(
                                                    QJsonDocument::Compact
                                                )
                                        )
                                    )
                                    .toStdString();
        const qint64 now = entry.timestampUtc.toMSecsSinceEpoch();
        const std::lock_guard<std::mutex> lock(impl_->repeatMutex);
        Impl::RepeatState &state = impl_->repeatStates[key];
        if (state.lastEmittedMs > 0 &&
            now - state.lastEmittedMs <
                impl_->options.repeatWindowMs) {
            ++state.suppressedCount;
            state.lastEntry = entry;
            return;
        }
        entry.repeatedCount = state.suppressedCount + 1;
        state.lastEmittedMs = now;
        state.suppressedCount = 0;
        state.lastEntry = entry;
    }
    impl_->enqueueSystem(entry);
}

void LogManager::logAudit(const AuditRecord &source)
{
    if (!isInitialized()) {
        return;
    }
    AuditRecord record = source;
    if (!record.timestampUtc.isValid()) {
        record.timestampUtc = QDateTime::currentDateTimeUtc();
    }
    record.actor = SensitiveDataSanitizer::sanitizeText(
        record.actor.trimmed()
    );
    record.targetType = SensitiveDataSanitizer::sanitizeText(
        record.targetType.trimmed()
    );
    record.targetId = SensitiveDataSanitizer::sanitizeText(
        record.targetId.trimmed()
    );
    record.reason = SensitiveDataSanitizer::sanitizeText(
        record.reason.trimmed()
    );
    record.beforeValues =
        SensitiveDataSanitizer::sanitizeObject(record.beforeValues);
    record.afterValues =
        SensitiveDataSanitizer::sanitizeObject(record.afterValues);
    record.source = SensitiveDataSanitizer::sanitizeText(
        record.source.trimmed()
    );
    impl_->enqueueAudit(record);
}

QString LogManager::sanitizeUrl(const QString &url)
{
    return SensitiveDataSanitizer::sanitizeUrl(url);
}

QString LogManager::levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return QStringLiteral("TRACE");
    case LogLevel::Debug:
        return QStringLiteral("DEBUG");
    case LogLevel::Info:
        return QStringLiteral("INFO");
    case LogLevel::Warning:
        return QStringLiteral("WARN");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    case LogLevel::Critical:
        return QStringLiteral("CRITICAL");
    }
    return QStringLiteral("UNKNOWN");
}

QString LogManager::auditActionName(AuditAction action)
{
    switch (action) {
    case AuditAction::Login: return QStringLiteral("LOGIN");
    case AuditAction::Logout: return QStringLiteral("LOGOUT");
    case AuditAction::AddDevice: return QStringLiteral("ADD_DEVICE");
    case AuditAction::RemoveDevice: return QStringLiteral("REMOVE_DEVICE");
    case AuditAction::UpdateDeviceName:
        return QStringLiteral("UPDATE_DEVICE_NAME");
    case AuditAction::UpdateDeviceConnection:
        return QStringLiteral("UPDATE_DEVICE_CONNECTION");
    case AuditAction::UpdateUserConfiguration:
        return QStringLiteral("UPDATE_USER_CONFIGURATION");
    case AuditAction::UpdatePermission:
        return QStringLiteral("UPDATE_PERMISSION");
    case AuditAction::ImportConfiguration:
        return QStringLiteral("IMPORT_CONFIGURATION");
    case AuditAction::ExportConfiguration:
        return QStringLiteral("EXPORT_CONFIGURATION");
    case AuditAction::ClearData: return QStringLiteral("CLEAR_DATA");
    case AuditAction::RestoreDefaults:
        return QStringLiteral("RESTORE_DEFAULTS");
    case AuditAction::SoftwareUpgrade:
        return QStringLiteral("SOFTWARE_UPGRADE");
    case AuditAction::DeviceUpgrade:
        return QStringLiteral("DEVICE_UPGRADE");
    case AuditAction::ManualReconnect:
        return QStringLiteral("MANUAL_RECONNECT");
    }
    return QStringLiteral("UNKNOWN");
}

QString LogManager::auditResultName(AuditResult result)
{
    switch (result) {
    case AuditResult::Success:
        return QStringLiteral("SUCCESS");
    case AuditResult::Failure:
        return QStringLiteral("FAILURE");
    case AuditResult::Cancelled:
        return QStringLiteral("CANCELLED");
    }
    return QStringLiteral("UNKNOWN");
}

bool LogManager::parseLevel(const QString &text, LogLevel *level)
{
    if (level == nullptr) {
        return false;
    }
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("trace")) {
        *level = LogLevel::Trace;
    } else if (normalized == QStringLiteral("debug")) {
        *level = LogLevel::Debug;
    } else if (normalized == QStringLiteral("info")) {
        *level = LogLevel::Info;
    } else if (normalized == QStringLiteral("warning") ||
               normalized == QStringLiteral("warn")) {
        *level = LogLevel::Warning;
    } else if (normalized == QStringLiteral("error")) {
        *level = LogLevel::Error;
    } else if (normalized == QStringLiteral("critical")) {
        *level = LogLevel::Critical;
    } else {
        return false;
    }
    return true;
}
