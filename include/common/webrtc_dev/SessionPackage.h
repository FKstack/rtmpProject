#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

namespace rtmp_monitor::webrtc_dev {

inline constexpr int kSessionSchemaVersion = 1;
inline constexpr qint64 kSessionLifetimeMs = 10 * 60 * 1000;
inline constexpr qint64 kFutureClockToleranceMs = 2 * 60 * 1000;
inline constexpr qsizetype kMaximumSessionFileBytes = 256 * 1024;
inline constexpr qsizetype kMaximumSdpBytes = 192 * 1024;

enum class SessionRole {
    Offer,
    Answer,
};

enum class SessionError {
    None,
    EmptyInput,
    InputTooLarge,
    InvalidUtf8,
    InvalidJson,
    InvalidRoot,
    InvalidFieldSet,
    InvalidFieldType,
    UnsupportedVersion,
    InvalidSessionId,
    InvalidRole,
    RoleDescriptionMismatch,
    InvalidTimestamp,
    InvalidLifetime,
    CreatedInFuture,
    Expired,
    SdpTooLarge,
    SessionMismatch,
    UnsafePath,
    NotFound,
    AmbiguousInput,
    IoFailure,
    AtomicWriteFailure,
    PermissionFailure,
};

struct SessionPackage
{
    int schemaVersion = kSessionSchemaVersion;
    QString sessionId;
    SessionRole role = SessionRole::Offer;
    QDateTime createdAtUtc;
    QDateTime expiresAtUtc;
    SessionRole descriptionType = SessionRole::Offer;
    QString sdp;
};

struct SessionResult
{
    SessionError error = SessionError::None;
    std::optional<SessionPackage> package;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == SessionError::None && package.has_value();
    }
};

struct SessionExpectation
{
    std::optional<SessionRole> role;
    QString sessionId;
    bool allowExpired = false;
};

struct SessionFileResult
{
    SessionError error = SessionError::None;
    QString filePath;
    qsizetype byteCount = 0;
    QByteArray bytes;

    [[nodiscard]] bool ok() const noexcept
    {
        return error == SessionError::None;
    }
};

class SessionPackageCodec final
{
public:
    SessionPackageCodec() = delete;

    [[nodiscard]] static SessionPackage create(
        SessionRole role,
        QString sdp,
        const QDateTime &nowUtc = QDateTime::currentDateTimeUtc(),
        QString sessionId = {}
    );
    [[nodiscard]] static QByteArray encode(const SessionPackage &package);
    [[nodiscard]] static SessionResult decodeAndValidate(
        const QByteArray &bytes,
        const QDateTime &nowUtc,
        const SessionExpectation &expectation = {}
    );
    [[nodiscard]] static QString roleName(SessionRole role);
    [[nodiscard]] static QString errorName(SessionError error);
    [[nodiscard]] static QString redactedSessionId(const QString &sessionId);
};

class SessionPackageStore final
{
public:
    explicit SessionPackageStore(QString rootPath);

    [[nodiscard]] static QString discoverRepositoryRoot(
        const QString &startPath
    );
    [[nodiscard]] static QString exchangeRootForRepository(
        const QString &repositoryRoot
    );

    [[nodiscard]] SessionError prepare();
    [[nodiscard]] SessionFileResult write(const SessionPackage &package);
    [[nodiscard]] SessionFileResult read(const QString &filePath) const;
    [[nodiscard]] SessionError remove(const QString &filePath) const;
    [[nodiscard]] QStringList managedFiles(SessionRole role) const;
    [[nodiscard]] SessionError cleanupExpired(
        const QDateTime &nowUtc,
        int *removedCount = nullptr
    ) const;
    [[nodiscard]] const QString &rootPath() const noexcept;

private:
    [[nodiscard]] QString filePathFor(const SessionPackage &package) const;
    [[nodiscard]] bool isManagedPath(
        const QString &filePath,
        std::optional<SessionRole> expectedRole = std::nullopt
    ) const;
    [[nodiscard]] static bool applyOwnerOnlyPermissions(
        const QString &path,
        bool directory
    );

    QString rootPath_;
};

} // namespace rtmp_monitor::webrtc_dev
