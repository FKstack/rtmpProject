#include <QtTest>

#include <QHash>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>

#include <utility>

#include "device_control/MqttDeviceClient.h"

class FakeMqttBroker final : public QObject
{
    Q_OBJECT
public:
    enum class SubscribeBehavior { Accept, Reject, Ignore };

    explicit FakeMqttBroker(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (server_.hasPendingConnections()) {
                QTcpSocket *socket = server_.nextPendingConnection();
                sockets_.insert(socket);
                ++connectionCount_;
                connect(socket, &QTcpSocket::readyRead, this,
                        [this, socket] { readPackets(socket); });
                connect(socket, &QTcpSocket::disconnected, this,
                        [this, socket] {
                            sockets_.remove(socket);
                            buffers_.remove(socket);
                            subscriptions_.remove(socket);
                            socket->deleteLater();
                        });
            }
        });
    }

    ~FakeMqttBroker() override
    {
        for (QTcpSocket *socket : std::as_const(sockets_)) {
            QObject::disconnect(socket, nullptr, this, nullptr);
            socket->abort();
        }
        sockets_.clear();
        buffers_.clear();
        subscriptions_.clear();
    }

    bool listen(quint16 port = 0)
    {
        return server_.listen(QHostAddress::LocalHost, port);
    }
    void setSubscribeBehavior(SubscribeBehavior behavior)
    {
        subscribeBehavior_ = behavior;
    }
    void dropClients()
    {
        for (QTcpSocket *socket : std::as_const(sockets_)) socket->abort();
    }
    void publishToSubscribers(const QByteArray &topic, const QByteArray &payload)
    {
        const QByteArray packet = publishPacket(topic, payload);
        for (QTcpSocket *socket : std::as_const(sockets_)) {
            if (subscriptions_.value(socket).contains(topic)) {
                socket->write(packet);
                socket->flush();
            }
        }
    }
    quint16 port() const { return server_.serverPort(); }
    int connectionCount() const { return connectionCount_; }
    int subscribeCount() const { return subscribeCount_; }
    int publishCount() const { return publishCount_; }
    QByteArray topic() const { return topic_; }
    QByteArray payload() const { return payload_; }

signals:
    void publishReceived();
    void subscribeReceived();
    void disconnectReceived();

private:
    static QByteArray remainingLength(int value)
    {
        QByteArray result;
        do {
            unsigned char byte = static_cast<unsigned char>(value % 128);
            value /= 128;
            if (value > 0) byte |= 0x80;
            result.append(static_cast<char>(byte));
        } while (value > 0);
        return result;
    }

    static QByteArray publishPacket(const QByteArray &topic,
                                    const QByteArray &payload)
    {
        QByteArray body;
        body.append(static_cast<char>((topic.size() >> 8) & 0xff));
        body.append(static_cast<char>(topic.size() & 0xff));
        body.append(topic);
        body.append(payload);
        return QByteArray(1, static_cast<char>(0x30)) +
               remainingLength(body.size()) + body;
    }

    static bool takeRemainingLength(const QByteArray &buffer, int offset,
                                    int *value, int *bytes)
    {
        int multiplier = 1;
        *value = 0;
        *bytes = 0;
        for (int index = offset; index < buffer.size() && *bytes < 4; ++index) {
            const unsigned char byte = static_cast<unsigned char>(buffer.at(index));
            *value += (byte & 127) * multiplier;
            ++*bytes;
            if ((byte & 128) == 0) return true;
            multiplier *= 128;
        }
        return false;
    }

    void readPackets(QTcpSocket *socket)
    {
        QByteArray &buffer = buffers_[socket];
        buffer.append(socket->readAll());
        while (buffer.size() >= 2) {
            int remaining = 0;
            int lengthBytes = 0;
            if (!takeRemainingLength(buffer, 1, &remaining, &lengthBytes)) return;
            const int headerBytes = 1 + lengthBytes;
            if (buffer.size() < headerBytes + remaining) return;
            const quint8 type = static_cast<quint8>(buffer.at(0)) >> 4;
            const QByteArray body = buffer.mid(headerBytes, remaining);
            buffer.remove(0, headerBytes + remaining);
            if (type == 1) {
                socket->write(QByteArray::fromHex("20020000"));
                socket->flush();
            } else if (type == 8 && body.size() >= 5) {
                const quint16 packetId =
                    (static_cast<unsigned char>(body.at(0)) << 8) |
                    static_cast<unsigned char>(body.at(1));
                QList<QByteArray> requestedTopics;
                int offset = 2;
                while (offset + 3 <= body.size()) {
                    const int topicLength =
                        (static_cast<unsigned char>(body.at(offset)) << 8) |
                        static_cast<unsigned char>(body.at(offset + 1));
                    offset += 2;
                    if (topicLength <= 0 || offset + topicLength + 1 > body.size()) {
                        requestedTopics.clear();
                        break;
                    }
                    requestedTopics.push_back(body.mid(offset, topicLength));
                    offset += topicLength + 1; // topic bytes and requested QoS
                }
                if (!requestedTopics.isEmpty()) {
                    subscribeCount_ += requestedTopics.size();
                    emit subscribeReceived();
                    if (subscribeBehavior_ == SubscribeBehavior::Accept) {
                        for (const QByteArray &topic : requestedTopics)
                            subscriptions_[socket].insert(topic);
                        QByteArray suback(1, static_cast<char>(0x90));
                        suback += remainingLength(2 + requestedTopics.size());
                        suback.append(static_cast<char>((packetId >> 8) & 0xff));
                        suback.append(static_cast<char>(packetId & 0xff));
                        suback.append(QByteArray(requestedTopics.size(),
                                                static_cast<char>(0x00)));
                        socket->write(suback);
                        socket->flush();
                    } else if (subscribeBehavior_ == SubscribeBehavior::Reject) {
                        QByteArray suback(1, static_cast<char>(0x90));
                        suback += remainingLength(2 + requestedTopics.size());
                        suback.append(static_cast<char>((packetId >> 8) & 0xff));
                        suback.append(static_cast<char>(packetId & 0xff));
                        suback.append(QByteArray(requestedTopics.size(),
                                                static_cast<char>(0x80)));
                        socket->write(suback);
                        socket->flush();
                    }
                }
            } else if (type == 3 && body.size() >= 2) {
                const int topicLength =
                    (static_cast<unsigned char>(body.at(0)) << 8) |
                    static_cast<unsigned char>(body.at(1));
                if (body.size() >= 2 + topicLength) {
                    topic_ = body.mid(2, topicLength);
                    payload_ = body.mid(2 + topicLength);
                    ++publishCount_;
                    emit publishReceived();
                    publishToSubscribers(topic_, payload_);
                }
            } else if (type == 12) {
                socket->write(QByteArray::fromHex("d000"));
                socket->flush();
            } else if (type == 14) {
                emit disconnectReceived();
            }
        }
    }

    QTcpServer server_;
    QSet<QTcpSocket *> sockets_;
    QHash<QTcpSocket *, QByteArray> buffers_;
    QHash<QTcpSocket *, QSet<QByteArray>> subscriptions_;
    SubscribeBehavior subscribeBehavior_ = SubscribeBehavior::Accept;
    QByteArray topic_;
    QByteArray payload_;
    int connectionCount_ = 0;
    int subscribeCount_ = 0;
    int publishCount_ = 0;
};

class MqttDeviceClientTest final : public QObject
{
    Q_OBJECT
private slots:
    void disabledBlankDefaultsDoNotOpenNetworkConnection();
    void enabledBlankBrokerIsRejected();
    void connectsSubscribesPublishesObservesAndStopsIdempotently();
    void fansOutToMultipleEqualClients();
    void rejectsPublishUntilSubackAndReportsRejectedSubscription();
    void timesOutWhenSubackIsMissing();
    void reconnectsAndSubscribesAgain();
    void retriesInitialConnectionFailure();
    void truncatesLargeObservedPayload();
    void boundsBurstInbox();
};

static MqttConnectionOptions optionsFor(const FakeMqttBroker &broker)
{
    MqttConnectionOptions options;
    options.enabled = true;
    options.brokerUrl = QStringLiteral("mqtt://127.0.0.1:%1").arg(broker.port());
    return options;
}

void MqttDeviceClientTest::disabledBlankDefaultsDoNotOpenNetworkConnection()
{
    FakeMqttBroker broker;
    QVERIFY(broker.listen());
    MqttDeviceClient client;
    client.connectToBroker(MqttConnectionOptions{});
    QCOMPARE(client.state(), MqttConnectionState::Disabled);
    QTest::qWait(100);
    QCOMPARE(broker.connectionCount(), 0);
}

void MqttDeviceClientTest::enabledBlankBrokerIsRejected()
{
    MqttConnectionOptions options;
    options.enabled = true;
    MqttDeviceClient client;
    client.connectToBroker(options);
    QCOMPARE(client.state(), MqttConnectionState::Error);
}

void MqttDeviceClientTest::connectsSubscribesPublishesObservesAndStopsIdempotently()
{
    FakeMqttBroker broker;
    QVERIFY(broker.listen());
    MqttDeviceClient client;
    QSignalSpy submittedSpy(&client, &MqttDeviceClient::commandSubmitted);
    QSignalSpy observedSpy(&client, &MqttDeviceClient::messageReceived);
    QSignalSpy publishSpy(&broker, &FakeMqttBroker::publishReceived);
    QSignalSpy disconnectSpy(&broker, &FakeMqttBroker::disconnectReceived);
    client.connectToBroker(optionsFor(broker));
    QTRY_COMPARE_WITH_TIMEOUT(broker.subscribeCount(), 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == MqttConnectionState::Connected, 3000);
    QCOMPARE(broker.publishCount(), 0);
    QVERIFY(client.publish(DeviceCommand::TurnLeft));
    QCOMPARE(submittedSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(publishSpy.count(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(observedSpy.count(), 1, 3000);
    QCOMPARE(broker.topic(), QByteArray("device/control"));
    const MqttObservedMessage observed =
        qvariant_cast<MqttObservedMessage>(observedSpy.takeFirst().at(0));
    QCOMPARE(observed.topic, QStringLiteral("device/control"));
    QVERIFY(observed.payload.contains("\"action\":\"moveCar\""));
    QCOMPARE(observed.originalPayloadSize, observed.payload.size());
    broker.publishToSubscribers(
        "device/status",
        R"({"type":"heartbeat","client_id":"local-device","timestamp":1})");
    QTRY_COMPARE_WITH_TIMEOUT(observedSpy.count(), 1, 3000);
    QCOMPARE(qvariant_cast<MqttObservedMessage>(
                 observedSpy.takeFirst().at(0)).topic,
             QStringLiteral("device/status"));
    broker.publishToSubscribers("device/control", "external-observation-only");
    QTRY_COMPARE_WITH_TIMEOUT(observedSpy.count(), 1, 3000);
    QCOMPARE(submittedSpy.count(), 1);
    QCOMPARE(broker.publishCount(), 1);
    client.disconnectFromBroker();
    QTRY_COMPARE_WITH_TIMEOUT(disconnectSpy.count(), 1, 1000);
    QVERIFY(!client.publish(DeviceCommand::StopCar));
    client.disconnectFromBroker();
    QCOMPARE(client.state(), MqttConnectionState::Disconnected);
}

void MqttDeviceClientTest::fansOutToMultipleEqualClients()
{
    FakeMqttBroker broker;
    QVERIFY(broker.listen());
    MqttDeviceClient first;
    MqttDeviceClient second;
    QSignalSpy firstObserved(&first, &MqttDeviceClient::messageReceived);
    QSignalSpy secondObserved(&second, &MqttDeviceClient::messageReceived);
    first.connectToBroker(optionsFor(broker));
    second.connectToBroker(optionsFor(broker));
    QTRY_VERIFY_WITH_TIMEOUT(first.state() == MqttConnectionState::Connected, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(second.state() == MqttConnectionState::Connected, 3000);
    QVERIFY(first.publishStartStream(
        QStringLiteral("rtmp://127.0.0.1:1935/live/local-device")));
    QTRY_COMPARE_WITH_TIMEOUT(firstObserved.count(), 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(secondObserved.count(), 1, 3000);
}

void MqttDeviceClientTest::rejectsPublishUntilSubackAndReportsRejectedSubscription()
{
    FakeMqttBroker broker;
    broker.setSubscribeBehavior(FakeMqttBroker::SubscribeBehavior::Ignore);
    QVERIFY(broker.listen());
    MqttDeviceClient client;
    QSignalSpy failedSpy(&client, &MqttDeviceClient::commandFailed);
    client.connectToBroker(optionsFor(broker));
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == MqttConnectionState::Subscribing,
                             3000);
    QVERIFY(!client.publish(DeviceCommand::StopCar));
    QCOMPARE(failedSpy.count(), 1);
    client.disconnectFromBroker();

    broker.setSubscribeBehavior(FakeMqttBroker::SubscribeBehavior::Reject);
    client.connectToBroker(optionsFor(broker));
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == MqttConnectionState::Error, 3000);
}

void MqttDeviceClientTest::timesOutWhenSubackIsMissing()
{
    FakeMqttBroker broker;
    broker.setSubscribeBehavior(FakeMqttBroker::SubscribeBehavior::Ignore);
    QVERIFY(broker.listen());
    MqttDeviceClient client;
    client.connectToBroker(optionsFor(broker));
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == MqttConnectionState::Subscribing,
                             3000);
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == MqttConnectionState::Error,
                             MqttDeviceClient::kSubscribeTimeoutMs + 1500);
}

void MqttDeviceClientTest::reconnectsAndSubscribesAgain()
{
    FakeMqttBroker broker;
    QVERIFY(broker.listen());
    MqttDeviceClient client;
    client.connectToBroker(optionsFor(broker));
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == MqttConnectionState::Connected, 3000);
    QCOMPARE(broker.subscribeCount(), 2);
    broker.dropClients();
    QTRY_VERIFY_WITH_TIMEOUT(broker.connectionCount() >= 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(broker.subscribeCount() >= 4, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == MqttConnectionState::Connected, 3000);
}

void MqttDeviceClientTest::retriesInitialConnectionFailure()
{
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
    const quint16 port = reservation.serverPort();
    reservation.close();
    MqttDeviceClient client;
    MqttConnectionOptions options;
    options.enabled = true;
    options.brokerUrl = QStringLiteral("mqtt://127.0.0.1:%1").arg(port);
    client.connectToBroker(options);
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == MqttConnectionState::Reconnecting,
                             3000);
    FakeMqttBroker broker;
    QVERIFY(broker.listen(port));
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == MqttConnectionState::Connected, 6000);
    QCOMPARE(broker.subscribeCount(), 2);
}

void MqttDeviceClientTest::truncatesLargeObservedPayload()
{
    FakeMqttBroker broker;
    QVERIFY(broker.listen());
    MqttDeviceClient client;
    QSignalSpy observedSpy(&client, &MqttDeviceClient::messageReceived);
    client.connectToBroker(optionsFor(broker));
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == MqttConnectionState::Connected, 3000);
    const QByteArray payload(5000, 'x');
    broker.publishToSubscribers("device/control", payload);
    QTRY_COMPARE_WITH_TIMEOUT(observedSpy.count(), 1, 3000);
    const MqttObservedMessage observed =
        qvariant_cast<MqttObservedMessage>(observedSpy.takeFirst().at(0));
    QCOMPARE(observed.originalPayloadSize, payload.size());
    QCOMPARE(observed.payload.size(),
             MqttDeviceClient::kMaximumObservedPayloadBytes);
}

void MqttDeviceClientTest::boundsBurstInbox()
{
    FakeMqttBroker broker;
    QVERIFY(broker.listen());
    MqttDeviceClient client;
    QSignalSpy observedSpy(&client, &MqttDeviceClient::messageReceived);
    QSignalSpy droppedSpy(&client, &MqttDeviceClient::observedMessagesDropped);
    client.connectToBroker(optionsFor(broker));
    QTRY_VERIFY_WITH_TIMEOUT(client.state() == MqttConnectionState::Connected, 3000);
    for (int index = 0; index < 256; ++index)
        broker.publishToSubscribers("device/control", QByteArray::number(index));
    QTRY_VERIFY_WITH_TIMEOUT(observedSpy.count() > 0, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(droppedSpy.count() > 0, 3000);
    QVERIFY(observedSpy.count() <= 256);
}

QTEST_MAIN(MqttDeviceClientTest)
#include "MqttDeviceClientTest.moc"
