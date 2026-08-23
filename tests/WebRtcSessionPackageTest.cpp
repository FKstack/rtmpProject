#include <QtTest>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "webrtc_dev/SessionPackage.h"

#ifdef Q_OS_WIN
#include <aclapi.h>
#include <windows.h>
#endif

using namespace rtmp_monitor::webrtc_dev;

namespace {

const QDateTime kNow = QDateTime::fromString(
    QStringLiteral("2026-08-20T12:00:00.000Z"), Qt::ISODateWithMs
);

QByteArray mutate(
    const SessionPackage &package,
    const std::function<void(QJsonObject &)> &mutation
)
{
    QJsonObject object = QJsonDocument::fromJson(
        SessionPackageCodec::encode(package)
    ).object();
    mutation(object);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

#ifdef Q_OS_WIN
bool hasCurrentUserOnlyDacl(const QString &path)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL dacl = nullptr;
    const DWORD queryResult = GetNamedSecurityInfoW(
        const_cast<LPWSTR>(
            reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16())
        ),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        &dacl,
        nullptr,
        &descriptor
    );
    if (queryResult != ERROR_SUCCESS || descriptor == nullptr || dacl == nullptr) {
        if (descriptor != nullptr) LocalFree(descriptor);
        return false;
    }

    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    const bool protectedDacl =
        GetSecurityDescriptorControl(descriptor, &control, &revision) != FALSE &&
        (control & SE_DACL_PROTECTED) != 0;

    HANDLE token = nullptr;
    DWORD tokenBytes = 0;
    bool ownerOnly = false;
    if (protectedDacl && dacl->AceCount == 1 &&
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE) {
        GetTokenInformation(token, TokenUser, nullptr, 0, &tokenBytes);
        QByteArray tokenStorage(
            static_cast<qsizetype>(tokenBytes), Qt::Uninitialized
        );
        if (tokenBytes > 0 &&
            GetTokenInformation(
                token, TokenUser, tokenStorage.data(), tokenBytes, &tokenBytes
            ) != FALSE) {
            void *rawAce = nullptr;
            if (GetAce(dacl, 0, &rawAce) != FALSE) {
                const auto *ace = static_cast<const ACCESS_ALLOWED_ACE *>(rawAce);
                const auto *tokenUser = reinterpret_cast<const TOKEN_USER *>(
                    tokenStorage.constData()
                );
                ownerOnly = ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
                            (ace->Mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS &&
                            EqualSid(
                                const_cast<DWORD *>(&ace->SidStart),
                                tokenUser->User.Sid
                            ) != FALSE;
            }
        }
    }
    if (token != nullptr) CloseHandle(token);
    LocalFree(descriptor);
    return ownerOnly;
}
#endif

} // namespace

class WebRtcSessionPackageTest final : public QObject
{
    Q_OBJECT

private slots:
    void validRoundTrip()
    {
        const SessionPackage package = SessionPackageCodec::create(
            SessionRole::Offer, QStringLiteral("v=0\r\n"), kNow
        );
        const SessionResult result = SessionPackageCodec::decodeAndValidate(
            SessionPackageCodec::encode(package),
            kNow,
            SessionExpectation {SessionRole::Offer, package.sessionId, false}
        );
        QVERIFY(result.ok());
        QVERIFY(result.package->schemaVersion == 1);
        QVERIFY(result.package->role == SessionRole::Offer);
        QVERIFY(result.package->descriptionType == SessionRole::Offer);
        QVERIFY(result.package->createdAtUtc.msecsTo(
                    result.package->expiresAtUtc) == kSessionLifetimeMs);
        QVERIFY(result.package->sdp == QStringLiteral("v=0\r\n"));
    }

    void rejectsStructuralErrors()
    {
        const SessionPackage package = SessionPackageCodec::create(
            SessionRole::Offer, QStringLiteral("v=0\r\n"), kNow
        );
        QVERIFY(
            SessionPackageCodec::decodeAndValidate({}, kNow).error ==
            SessionError::EmptyInput
        );
        QVERIFY(
            SessionPackageCodec::decodeAndValidate("[]", kNow).error ==
            SessionError::InvalidRoot
        );
        QVERIFY(
            SessionPackageCodec::decodeAndValidate("{", kNow).error ==
            SessionError::InvalidJson
        );

        const QByteArray missing = mutate(package, [](QJsonObject &object) {
            object.remove(QStringLiteral("sdp"));
        });
        QVERIFY(SessionPackageCodec::decodeAndValidate(missing, kNow).error ==
                SessionError::InvalidFieldSet);

        const QByteArray extra = mutate(package, [](QJsonObject &object) {
            object.insert(QStringLiteral("candidate"), QStringLiteral("blocked"));
        });
        QVERIFY(SessionPackageCodec::decodeAndValidate(extra, kNow).error ==
                SessionError::InvalidFieldSet);

        const QByteArray wrongType = mutate(package, [](QJsonObject &object) {
            object.insert(QStringLiteral("sdp"), 7);
        });
        QVERIFY(SessionPackageCodec::decodeAndValidate(wrongType, kNow).error ==
                SessionError::InvalidFieldType);

        const QByteArray unknownVersion = mutate(package, [](QJsonObject &object) {
            object.insert(QStringLiteral("schemaVersion"), 2);
        });
        QVERIFY(
            SessionPackageCodec::decodeAndValidate(unknownVersion, kNow).error ==
            SessionError::UnsupportedVersion
        );
    }

    void rejectsSemanticErrors()
    {
        const SessionPackage package = SessionPackageCodec::create(
            SessionRole::Offer, QStringLiteral("v=0\r\n"), kNow
        );
        const QByteArray roleMismatch = mutate(package, [](QJsonObject &object) {
            object.insert(QStringLiteral("descriptionType"), QStringLiteral("answer"));
        });
        QVERIFY(
            SessionPackageCodec::decodeAndValidate(roleMismatch, kNow).error ==
            SessionError::RoleDescriptionMismatch
        );

        const QByteArray invalidRole = mutate(package, [](QJsonObject &object) {
            object.insert(QStringLiteral("role"), QStringLiteral("sender"));
        });
        QVERIFY(SessionPackageCodec::decodeAndValidate(invalidRole, kNow).error ==
                SessionError::InvalidRole);

        const QByteArray invalidId = mutate(package, [](QJsonObject &object) {
            object.insert(QStringLiteral("sessionId"), QStringLiteral("device-name"));
        });
        QVERIFY(SessionPackageCodec::decodeAndValidate(invalidId, kNow).error ==
                SessionError::InvalidSessionId);

        const QByteArray wrongLifetime = mutate(package, [](QJsonObject &object) {
            object.insert(
                QStringLiteral("expiresAtUtc"),
                QStringLiteral("2026-08-20T12:11:00.000Z")
            );
        });
        QVERIFY(
            SessionPackageCodec::decodeAndValidate(wrongLifetime, kNow).error ==
            SessionError::InvalidLifetime
        );

        const QByteArray future = mutate(package, [](QJsonObject &object) {
            object.insert(
                QStringLiteral("createdAtUtc"),
                QStringLiteral("2026-08-20T12:03:00.000Z")
            );
            object.insert(
                QStringLiteral("expiresAtUtc"),
                QStringLiteral("2026-08-20T12:13:00.000Z")
            );
        });
        QVERIFY(SessionPackageCodec::decodeAndValidate(future, kNow).error ==
                SessionError::CreatedInFuture);

        QVERIFY(
            SessionPackageCodec::decodeAndValidate(
                SessionPackageCodec::encode(package),
                kNow.addMSecs(kSessionLifetimeMs),
                {}
            ).error == SessionError::Expired
        );

        QVERIFY(
            SessionPackageCodec::decodeAndValidate(
                SessionPackageCodec::encode(package),
                kNow,
                SessionExpectation {
                    SessionRole::Answer, package.sessionId, false
                }
            ).error == SessionError::InvalidRole
        );
        QVERIFY(
            SessionPackageCodec::decodeAndValidate(
                SessionPackageCodec::encode(package),
                kNow,
                SessionExpectation {
                    SessionRole::Offer,
                    QStringLiteral("68f0d3ca-4bc3-4e78-8459-a9b5d4478a95"),
                    false,
                }
            ).error == SessionError::SessionMismatch
        );
    }

    void rejectsInvalidUtf8AndOversize()
    {
        QByteArray invalidUtf8("{\"x\":\"");
        invalidUtf8.append(QByteArray::fromHex("c3"));
        invalidUtf8.append("\"}");
        QVERIFY(
            SessionPackageCodec::decodeAndValidate(invalidUtf8, kNow).error ==
            SessionError::InvalidUtf8
        );

        const QByteArray huge(kMaximumSessionFileBytes + 1, 'x');
        QVERIFY(SessionPackageCodec::decodeAndValidate(huge, kNow).error ==
                SessionError::InputTooLarge);

        SessionPackage package = SessionPackageCodec::create(
            SessionRole::Offer,
            QString(kMaximumSdpBytes + 1, QLatin1Char('x')),
            kNow
        );
        QVERIFY(
            SessionPackageCodec::decodeAndValidate(
                SessionPackageCodec::encode(package), kNow
            ).error == SessionError::SdpTooLarge
        );
    }

    void atomicStoreAndBoundedCleanup()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SessionPackageStore store(directory.filePath(QStringLiteral("exchange")));
        QVERIFY(store.prepare() == SessionError::None);

        const SessionPackage valid = SessionPackageCodec::create(
            SessionRole::Offer, QStringLiteral("v=0\r\n"), kNow
        );
        const SessionFileResult validWrite = store.write(valid);
        QVERIFY(validWrite.ok());
        QVERIFY(QFileInfo::exists(validWrite.filePath));
#ifdef Q_OS_WIN
        QVERIFY(hasCurrentUserOnlyDacl(store.rootPath()));
        QVERIFY(hasCurrentUserOnlyDacl(validWrite.filePath));
#endif
        QVERIFY(store.read(validWrite.filePath).bytes ==
                SessionPackageCodec::encode(valid));

        const QString outside = directory.filePath(
            QStringLiteral("68f0d3ca-4bc3-4e78-8459-a9b5d4478a95.offer.json")
        );
        QFile outsideFile(outside);
        QVERIFY(outsideFile.open(QIODevice::WriteOnly));
        QVERIFY(outsideFile.write("keep") == 4);
        outsideFile.close();
        QVERIFY(store.remove(outside) == SessionError::UnsafePath);
        QVERIFY(QFileInfo::exists(outside));

        const SessionPackage expired = SessionPackageCodec::create(
            SessionRole::Answer,
            QStringLiteral("v=0\r\n"),
            kNow.addMSecs(-kSessionLifetimeMs),
            valid.sessionId
        );
        const SessionFileResult expiredWrite = store.write(expired);
        QVERIFY(expiredWrite.ok());
        int removed = -1;
        const SessionError cleanupError = store.cleanupExpired(kNow, &removed);
        QVERIFY2(
            cleanupError == SessionError::None,
            qPrintable(SessionPackageCodec::errorName(cleanupError))
        );
        QVERIFY(removed == 1);
        QVERIFY(!QFileInfo::exists(expiredWrite.filePath));
        QVERIFY(QFileInfo::exists(validWrite.filePath));
        QVERIFY(QFileInfo::exists(outside));

        const QString malformed = QDir(store.rootPath()).filePath(
            QStringLiteral(
                "c9d03a6d-a8db-4e7a-8328-c266af90431c.answer.json"
            )
        );
        QFile malformedFile(malformed);
        QVERIFY(malformedFile.open(QIODevice::WriteOnly));
        QVERIFY(malformedFile.write("{") == 1);
        malformedFile.close();
        removed = -1;
        QVERIFY(store.cleanupExpired(kNow, &removed) == SessionError::None);
        QVERIFY(removed == 0);
        QVERIFY(QFileInfo::exists(malformed));

        QVERIFY(store.remove(validWrite.filePath) == SessionError::None);
        QVERIFY(!QFileInfo::exists(validWrite.filePath));
    }

    void safeIdentifiersAndErrors()
    {
        const SessionPackage package = SessionPackageCodec::create(
            SessionRole::Offer, QStringLiteral("v=0\r\n"), kNow
        );
        const QString redacted = SessionPackageCodec::redactedSessionId(
            package.sessionId
        );
        QVERIFY(redacted.size() == 8);
        QVERIFY(!redacted.contains(package.sessionId));
        for (int value = static_cast<int>(SessionError::None);
             value <= static_cast<int>(SessionError::PermissionFailure);
             ++value) {
            const QString name = SessionPackageCodec::errorName(
                static_cast<SessionError>(value)
            );
            QVERIFY(!name.isEmpty());
            QVERIFY(!name.contains(QLatin1Char('/')));
            QVERIFY(!name.contains(QLatin1Char(':')));
        }
    }
};

QTEST_GUILESS_MAIN(WebRtcSessionPackageTest)
#include "WebRtcSessionPackageTest.moc"
