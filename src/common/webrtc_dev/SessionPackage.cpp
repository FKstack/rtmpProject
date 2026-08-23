#include "webrtc_dev/SessionPackage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <sddl.h>
#endif

#include <cmath>
#include <utility>

namespace rtmp_monitor::webrtc_dev {
namespace {

const QSet<QString> kRequiredFields {
    QStringLiteral("schemaVersion"),
    QStringLiteral("sessionId"),
    QStringLiteral("role"),
    QStringLiteral("createdAtUtc"),
    QStringLiteral("expiresAtUtc"),
    QStringLiteral("descriptionType"),
    QStringLiteral("sdp"),
};

const QRegularExpression kTimestampPattern(
    QStringLiteral(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$)"
    )
);

const QRegularExpression kManagedFilePattern(
    QStringLiteral(
        R"(^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}\.(offer|answer)\.json$)"
    )
);

std::optional<SessionRole> parseRole(const QString &value)
{
    if (value == QStringLiteral("offer")) return SessionRole::Offer;
    if (value == QStringLiteral("answer")) return SessionRole::Answer;
    return std::nullopt;
}

bool validUuidV4(const QString &value)
{
    const QUuid uuid(value);
    return !uuid.isNull() && uuid.version() == QUuid::Random &&
           uuid.toString(QUuid::WithoutBraces).toLower() == value;
}

std::optional<QDateTime> parseTimestamp(const QString &value)
{
    if (!kTimestampPattern.match(value).hasMatch()) return std::nullopt;
    const QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!parsed.isValid()) return std::nullopt;
    return parsed.toUTC();
}

SessionResult fail(SessionError error)
{
    return {error, std::nullopt};
}

#ifdef Q_OS_WIN
bool currentUserSidString(QString *sidString)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    if (bytes == 0) {
        CloseHandle(token);
        return false;
    }

    QByteArray storage(static_cast<qsizetype>(bytes), Qt::Uninitialized);
    if (!GetTokenInformation(
            token,
            TokenUser,
            storage.data(),
            bytes,
            &bytes
        )) {
        CloseHandle(token);
        return false;
    }
    CloseHandle(token);

    const auto *user = reinterpret_cast<const TOKEN_USER *>(storage.constData());
    LPWSTR rawSid = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &rawSid)) return false;
    *sidString = QString::fromWCharArray(rawSid);
    LocalFree(rawSid);
    return !sidString->isEmpty();
}
#endif

} // namespace

SessionPackage SessionPackageCodec::create(
    SessionRole role,
    QString sdp,
    const QDateTime &nowUtc,
    QString sessionId
)
{
    if (sessionId.isEmpty()) {
        sessionId = QUuid::createUuid()
                        .toString(QUuid::WithoutBraces)
                        .toLower();
    }
    const QDateTime created = nowUtc.toUTC();
    return {
        kSessionSchemaVersion,
        std::move(sessionId),
        role,
        created,
        created.addMSecs(kSessionLifetimeMs),
        role,
        std::move(sdp),
    };
}

QByteArray SessionPackageCodec::encode(const SessionPackage &package)
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), package.schemaVersion);
    object.insert(QStringLiteral("sessionId"), package.sessionId);
    object.insert(QStringLiteral("role"), roleName(package.role));
    object.insert(
        QStringLiteral("createdAtUtc"),
        package.createdAtUtc.toUTC().toString(Qt::ISODateWithMs)
    );
    object.insert(
        QStringLiteral("expiresAtUtc"),
        package.expiresAtUtc.toUTC().toString(Qt::ISODateWithMs)
    );
    object.insert(
        QStringLiteral("descriptionType"),
        roleName(package.descriptionType)
    );
    object.insert(QStringLiteral("sdp"), package.sdp);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

SessionResult SessionPackageCodec::decodeAndValidate(
    const QByteArray &bytes,
    const QDateTime &nowUtc,
    const SessionExpectation &expectation
)
{
    if (bytes.isEmpty()) return fail(SessionError::EmptyInput);
    if (bytes.size() > kMaximumSessionFileBytes) {
        return fail(SessionError::InputTooLarge);
    }

    if (!bytes.isValidUtf8()) return fail(SessionError::InvalidUtf8);

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(SessionError::InvalidJson);
    }
    if (!document.isObject()) return fail(SessionError::InvalidRoot);

    const QJsonObject object = document.object();
    const QStringList objectKeys = object.keys();
    const QSet<QString> actualFields(
        objectKeys.cbegin(), objectKeys.cend()
    );
    if (actualFields != kRequiredFields) {
        return fail(SessionError::InvalidFieldSet);
    }

    const QJsonValue versionValue = object.value(QStringLiteral("schemaVersion"));
    if (!versionValue.isDouble() ||
        std::floor(versionValue.toDouble()) != versionValue.toDouble()) {
        return fail(SessionError::InvalidFieldType);
    }
    if (versionValue.toInt() != kSessionSchemaVersion) {
        return fail(SessionError::UnsupportedVersion);
    }

    for (const QString &field : {
             QStringLiteral("sessionId"),
             QStringLiteral("role"),
             QStringLiteral("createdAtUtc"),
             QStringLiteral("expiresAtUtc"),
             QStringLiteral("descriptionType"),
             QStringLiteral("sdp"),
         }) {
        if (!object.value(field).isString()) {
            return fail(SessionError::InvalidFieldType);
        }
    }

    SessionPackage package;
    package.sessionId = object.value(QStringLiteral("sessionId")).toString();
    if (!validUuidV4(package.sessionId)) {
        return fail(SessionError::InvalidSessionId);
    }

    const auto role = parseRole(object.value(QStringLiteral("role")).toString());
    const auto description = parseRole(
        object.value(QStringLiteral("descriptionType")).toString()
    );
    if (!role.has_value() || !description.has_value()) {
        return fail(SessionError::InvalidRole);
    }
    package.role = *role;
    package.descriptionType = *description;
    if (package.role != package.descriptionType) {
        return fail(SessionError::RoleDescriptionMismatch);
    }

    const auto created = parseTimestamp(
        object.value(QStringLiteral("createdAtUtc")).toString()
    );
    const auto expires = parseTimestamp(
        object.value(QStringLiteral("expiresAtUtc")).toString()
    );
    if (!created.has_value() || !expires.has_value()) {
        return fail(SessionError::InvalidTimestamp);
    }
    package.createdAtUtc = *created;
    package.expiresAtUtc = *expires;
    if (package.createdAtUtc >= package.expiresAtUtc ||
        package.createdAtUtc.msecsTo(package.expiresAtUtc) !=
            kSessionLifetimeMs) {
        return fail(SessionError::InvalidLifetime);
    }

    const QDateTime now = nowUtc.toUTC();
    if (now.msecsTo(package.createdAtUtc) > kFutureClockToleranceMs) {
        return fail(SessionError::CreatedInFuture);
    }
    if (!expectation.allowExpired && package.expiresAtUtc <= now) {
        return fail(SessionError::Expired);
    }

    package.sdp = object.value(QStringLiteral("sdp")).toString();
    if (package.sdp.isEmpty()) return fail(SessionError::InvalidFieldType);
    if (package.sdp.toUtf8().size() > kMaximumSdpBytes) {
        return fail(SessionError::SdpTooLarge);
    }

    if (expectation.role.has_value() && package.role != *expectation.role) {
        return fail(SessionError::InvalidRole);
    }
    if (!expectation.sessionId.isEmpty() &&
        package.sessionId != expectation.sessionId) {
        return fail(SessionError::SessionMismatch);
    }

    package.schemaVersion = kSessionSchemaVersion;
    return {SessionError::None, std::move(package)};
}

QString SessionPackageCodec::roleName(SessionRole role)
{
    return role == SessionRole::Offer
               ? QStringLiteral("offer")
               : QStringLiteral("answer");
}

QString SessionPackageCodec::errorName(SessionError error)
{
    switch (error) {
    case SessionError::None: return QStringLiteral("none");
    case SessionError::EmptyInput: return QStringLiteral("empty_input");
    case SessionError::InputTooLarge: return QStringLiteral("input_too_large");
    case SessionError::InvalidUtf8: return QStringLiteral("invalid_utf8");
    case SessionError::InvalidJson: return QStringLiteral("invalid_json");
    case SessionError::InvalidRoot: return QStringLiteral("invalid_root");
    case SessionError::InvalidFieldSet: return QStringLiteral("invalid_field_set");
    case SessionError::InvalidFieldType: return QStringLiteral("invalid_field_type");
    case SessionError::UnsupportedVersion: return QStringLiteral("unsupported_version");
    case SessionError::InvalidSessionId: return QStringLiteral("invalid_session_id");
    case SessionError::InvalidRole: return QStringLiteral("invalid_role");
    case SessionError::RoleDescriptionMismatch: return QStringLiteral("role_mismatch");
    case SessionError::InvalidTimestamp: return QStringLiteral("invalid_timestamp");
    case SessionError::InvalidLifetime: return QStringLiteral("invalid_lifetime");
    case SessionError::CreatedInFuture: return QStringLiteral("created_in_future");
    case SessionError::Expired: return QStringLiteral("expired");
    case SessionError::SdpTooLarge: return QStringLiteral("sdp_too_large");
    case SessionError::SessionMismatch: return QStringLiteral("session_mismatch");
    case SessionError::UnsafePath: return QStringLiteral("unsafe_path");
    case SessionError::NotFound: return QStringLiteral("not_found");
    case SessionError::AmbiguousInput: return QStringLiteral("ambiguous_input");
    case SessionError::IoFailure: return QStringLiteral("io_failure");
    case SessionError::AtomicWriteFailure: return QStringLiteral("atomic_write_failure");
    case SessionError::PermissionFailure: return QStringLiteral("permission_failure");
    }
    return QStringLiteral("unknown");
}

QString SessionPackageCodec::redactedSessionId(const QString &sessionId)
{
    const QByteArray digest = QCryptographicHash::hash(
        sessionId.toUtf8(), QCryptographicHash::Sha256
    ).toHex();
    return QString::fromLatin1(digest.left(8));
}

SessionPackageStore::SessionPackageStore(QString rootPath)
    : rootPath_(QDir::cleanPath(QFileInfo(std::move(rootPath)).absoluteFilePath()))
{
}

QString SessionPackageStore::discoverRepositoryRoot(const QString &startPath)
{
    QDir directory(QFileInfo(startPath).absoluteFilePath());
    for (int depth = 0; depth < 32; ++depth) {
        if (QFileInfo(directory.filePath(QStringLiteral("CMakeLists.txt"))).isFile() &&
            QFileInfo::exists(directory.filePath(QStringLiteral(".git")))) {
            return directory.absolutePath();
        }
        if (!directory.cdUp()) break;
    }
    return {};
}

QString SessionPackageStore::exchangeRootForRepository(
    const QString &repositoryRoot
)
{
    if (repositoryRoot.isEmpty()) return {};
    return QDir(repositoryRoot).absoluteFilePath(
        QStringLiteral("out/webrtc-p2p/session-exchange")
    );
}

SessionError SessionPackageStore::prepare()
{
    if (rootPath_.isEmpty()) return SessionError::UnsafePath;
    QDir directory;
    if (!directory.mkpath(rootPath_)) return SessionError::IoFailure;
    const QString canonical = QFileInfo(rootPath_).canonicalFilePath();
    if (canonical.isEmpty()) return SessionError::UnsafePath;
    rootPath_ = QDir::cleanPath(canonical);
    if (!applyOwnerOnlyPermissions(rootPath_, true)) {
        return SessionError::PermissionFailure;
    }
    return SessionError::None;
}

SessionFileResult SessionPackageStore::write(const SessionPackage &package)
{
    const QByteArray bytes = SessionPackageCodec::encode(package);
    if (bytes.size() > kMaximumSessionFileBytes) {
        return {SessionError::InputTooLarge, {}, bytes.size(), {}};
    }
    const QString destination = filePathFor(package);
    if (!isManagedPath(destination, package.role)) {
        return {SessionError::UnsafePath, {}, bytes.size(), {}};
    }

    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) {
        output.cancelWriting();
        return {SessionError::IoFailure, {}, bytes.size(), {}};
    }
    if (!output.commit()) {
        return {SessionError::AtomicWriteFailure, {}, bytes.size(), {}};
    }
    if (!applyOwnerOnlyPermissions(destination, false)) {
        QFile::remove(destination);
        return {SessionError::PermissionFailure, {}, bytes.size(), {}};
    }
    return {SessionError::None, destination, bytes.size(), {}};
}

SessionFileResult SessionPackageStore::read(const QString &filePath) const
{
    if (!isManagedPath(filePath)) {
        return {SessionError::UnsafePath, {}, 0, {}};
    }
    QFile input(filePath);
    if (!input.exists()) return {SessionError::NotFound, {}, 0, {}};
    if (input.size() > kMaximumSessionFileBytes) {
        return {SessionError::InputTooLarge, filePath, input.size(), {}};
    }
    if (!input.open(QIODevice::ReadOnly)) {
        return {SessionError::IoFailure, filePath, 0, {}};
    }
    const QByteArray bytes = input.read(kMaximumSessionFileBytes + 1);
    if (bytes.size() > kMaximumSessionFileBytes) {
        return {SessionError::InputTooLarge, filePath, bytes.size(), {}};
    }
    return {SessionError::None, filePath, bytes.size(), bytes};
}

SessionError SessionPackageStore::remove(const QString &filePath) const
{
    if (!isManagedPath(filePath)) return SessionError::UnsafePath;
    if (!QFileInfo::exists(filePath)) return SessionError::NotFound;
    return QFile::remove(filePath) ? SessionError::None : SessionError::IoFailure;
}

QStringList SessionPackageStore::managedFiles(SessionRole role) const
{
    QDir directory(rootPath_);
    const QString suffix = QStringLiteral(".%1.json").arg(
        SessionPackageCodec::roleName(role)
    );
    QStringList result;
    const QFileInfoList entries = directory.entryInfoList(
        {QStringLiteral("*.json")},
        QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
        QDir::Name
    );
    for (const QFileInfo &entry : entries) {
        if (entry.fileName().endsWith(suffix) &&
            isManagedPath(entry.absoluteFilePath(), role)) {
            result.push_back(entry.absoluteFilePath());
        }
    }
    return result;
}

SessionError SessionPackageStore::cleanupExpired(
    const QDateTime &nowUtc,
    int *removedCount
) const
{
    int removed = 0;
    for (const SessionRole role : {SessionRole::Offer, SessionRole::Answer}) {
        for (const QString &path : managedFiles(role)) {
            QFile input(path);
            if (input.size() > kMaximumSessionFileBytes ||
                !input.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QByteArray bytes = input.read(kMaximumSessionFileBytes + 1);
            const SessionResult result = SessionPackageCodec::decodeAndValidate(
                bytes,
                nowUtc,
                SessionExpectation {role, {}, true}
            );
            input.close();
            if (!result.ok() || result.package->expiresAtUtc > nowUtc.toUTC()) {
                continue;
            }
            const SessionError removeError = remove(path);
            if (removeError != SessionError::None) return removeError;
            ++removed;
        }
    }
    if (removedCount != nullptr) *removedCount = removed;
    return SessionError::None;
}

const QString &SessionPackageStore::rootPath() const noexcept
{
    return rootPath_;
}

QString SessionPackageStore::filePathFor(const SessionPackage &package) const
{
    return QDir(rootPath_).absoluteFilePath(
        QStringLiteral("%1.%2.json")
            .arg(package.sessionId, SessionPackageCodec::roleName(package.role))
    );
}

bool SessionPackageStore::isManagedPath(
    const QString &filePath,
    std::optional<SessionRole> expectedRole
) const
{
    const QFileInfo rootInfo(rootPath_);
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty()) return false;

    const QFileInfo fileInfo(filePath);
    if (fileInfo.isSymLink() || fileInfo.fileName().isEmpty() ||
        !kManagedFilePattern.match(fileInfo.fileName()).hasMatch()) {
        return false;
    }
    if (expectedRole.has_value()) {
        const QString expectedSuffix = QStringLiteral(".%1.json").arg(
            SessionPackageCodec::roleName(*expectedRole)
        );
        if (!fileInfo.fileName().endsWith(expectedSuffix)) return false;
    }

    const QString canonicalParent = fileInfo.dir().canonicalPath();
    const Qt::CaseSensitivity sensitivity =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    return !canonicalParent.isEmpty() &&
           canonicalParent.compare(canonicalRoot, sensitivity) == 0;
}

bool SessionPackageStore::applyOwnerOnlyPermissions(
    const QString &path,
    bool directory
)
{
#ifdef Q_OS_WIN
    QString sid;
    if (!currentUserSidString(&sid)) return false;
    const QString sddl = directory
        ? QStringLiteral("D:P(A;OICI;FA;;;%1)").arg(sid)
        : QStringLiteral("D:P(A;;FA;;;%1)").arg(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            reinterpret_cast<LPCWSTR>(sddl.utf16()),
            SDDL_REVISION_1,
            &descriptor,
            nullptr
        )) {
        return false;
    }
    const BOOL applied = SetFileSecurityW(
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        descriptor
    );
    LocalFree(descriptor);
    return applied == TRUE;
#else
    QFileDevice::Permissions permissions =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (directory) permissions |= QFileDevice::ExeOwner;
    return QFile::setPermissions(path, permissions);
#endif
}

} // namespace rtmp_monitor::webrtc_dev
