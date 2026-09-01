#include "signaling_session/DirectSessionCore.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <utility>

namespace rtmp::p2p {
namespace {
std::int64_t systemNow()
{
    return QDateTime::currentMSecsSinceEpoch();
}

std::string uuidV4()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

std::string timestamp(std::int64_t milliseconds)
{
    return QDateTime::fromMSecsSinceEpoch(milliseconds, Qt::UTC)
        .toString(Qt::ISODateWithMs).toStdString();
}

std::int64_t parseTimestamp(const std::string &value)
{
    const QDateTime parsed = QDateTime::fromString(
        QString::fromStdString(value), Qt::ISODateWithMs);
    return parsed.isValid() ? parsed.toMSecsSinceEpoch() : -1;
}

std::string nonceFrom(const std::string &id)
{
    std::string result;
    for (const char ch : id) if (ch != '-') result.push_back(ch);
    result.append("directnonce");
    result.resize(43, '0');
    return result;
}

std::optional<std::string> topic(const DirectRouteIdentity &identity,
                                 TopicKind kind)
{
    TopicRoute route;
    route.kind = kind;
    route.deviceId = identity.deviceId;
    route.operatorId = identity.operatorId;
    route.clientInstanceId = identity.clientInstanceId;
    return TopicCodec::encode(route);
}

bool matches(const DirectRouteIdentity &identity, const SignalingFrame &frame,
             TopicKind kind)
{
    const auto expected = topic(identity, kind);
    return expected && *expected == frame.topic;
}

bool validFramePolicy(const SignalingFrame &frame, const Envelope &envelope)
{
    const auto policy = policyFor(envelope.messageType);
    if (!policy || frame.qos != policy->qos
        || frame.retained != policy->retained) return false;
    return frame.expirySeconds == 0
        || frame.expirySeconds <= policy->messageExpirySeconds;
}

std::optional<std::string> payloadString(const std::string &json,
                                         const char *field)
{
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(json));
    if (!document.isObject() || !document.object().value(field).isString())
        return std::nullopt;
    return document.object().value(field).toString().toStdString();
}

Envelope replyEnvelope(const Envelope &request, const DirectRouteIdentity &identity,
                       const std::string &type, const std::string &payload,
                       const DirectClock &clock, const DirectIdFactory &ids)
{
    Envelope reply;
    reply.messageId = ids();
    reply.messageType = type;
    reply.sentAtUtc = timestamp(clock());
    reply.ttlMs = policyFor(type)->maximumTtlMs;
    reply.sourceKind = "device";
    reply.sourceId = identity.deviceId;
    reply.targetKind = "operator";
    reply.targetId = identity.operatorId;
    reply.sessionId = request.sessionId;
    reply.attemptId = request.attemptId;
    reply.correlationId = request.messageId;
    reply.sequence = request.sequence;
    reply.sessionNonce = request.sessionNonce;
    reply.payloadJson = payload;
    return reply;
}

SignalingPublish encodedPublish(const Envelope &envelope,
                                const std::string &route)
{
    const auto policy = policyFor(envelope.messageType);
    const auto bytes = EnvelopeCodec::encode(envelope);
    if (!policy || !bytes) return {};
    return {route, *bytes, policy->qos, policy->retained,
            policy->messageExpirySeconds};
}
} // namespace

DirectOperatorCore::DirectOperatorCore(ISignalingChannel &channel,
                                       DirectRouteIdentity identity,
                                       DirectClock clock,
                                       DirectIdFactory ids)
    : channel_(channel), identity_(std::move(identity)),
      clock_(clock ? std::move(clock) : DirectClock(systemNow)),
      ids_(ids ? std::move(ids) : DirectIdFactory(uuidV4))
{
}

bool DirectOperatorCore::start()
{
    const auto response = topic(identity_, TopicKind::SignalToOperator);
    const auto presence = topic(identity_, TopicKind::Presence);
    if (!response || !presence) {
        snapshot_.lastError = "invalid_route";
        changed();
        return false;
    }
    channel_.setMessageHandler(
        [this](const SignalingFrame &frame) { receive(frame); });
    channel_.setStateHandler([this](SignalingChannelState state,
                                    const std::string &detail) {
        snapshot_.channelState = state;
        if (state == SignalingChannelState::Error) {
            snapshot_.lastError = detail.empty() ? "channel_error" : detail;
            changed();
        }
    });
    return channel_.start({*response, *presence});
}

void DirectOperatorCore::stop()
{
    channel_.stop();
}

bool DirectOperatorCore::requestStartStream()
{
    if (!SessionTransitionPolicy::validate(snapshot_.sessionState,
                                           SessionState::Requested).ok) {
        snapshot_.lastError = "invalid_transition";
        changed();
        return false;
    }
    sessionId_ = ids_();
    attemptId_ = ids_();
    nonce_ = nonceFrom(ids_());
    const std::string payload = "{\"requestNonce\":\"" + nonce_
        + "\",\"requestedDeviceId\":\"" + identity_.deviceId
        + "\",\"requestedRole\":\"viewer\",\"requestedScopes\":[\"video\"],"
          "\"capabilitiesRevision\":0}";
    updateState(SessionState::Requested);
    if (!publishSessionMessage("session.request", payload, true)) {
        snapshot_.sessionState = SessionState::Failed;
        changed();
        return false;
    }
    return true;
}

bool DirectOperatorCore::requestStopStream()
{
    if (!SessionTransitionPolicy::validate(snapshot_.sessionState,
                                           SessionState::Closing).ok) {
        snapshot_.lastError = "invalid_transition";
        changed();
        return false;
    }
    updateState(SessionState::Closing);
    if (!publishSessionMessage("session.cancel",
                               "{\"reasonCode\":\"operator_requested\"}",
                               true)) {
        snapshot_.sessionState = SessionState::Failed;
        changed();
        return false;
    }
    return true;
}

bool DirectOperatorCore::publishSessionMessage(const std::string &type,
                                               const std::string &payload,
                                               bool trackAck)
{
    const auto route = topic(identity_, TopicKind::SignalToDevice);
    const auto policy = policyFor(type);
    if (!route || !policy) return false;
    Envelope envelope;
    envelope.messageId = ids_();
    envelope.messageType = type;
    envelope.sentAtUtc = timestamp(clock_());
    envelope.ttlMs = policy->maximumTtlMs;
    envelope.sourceKind = "operator";
    envelope.sourceId = identity_.operatorId;
    envelope.sourceClientInstanceId = identity_.clientInstanceId;
    envelope.targetKind = "device";
    envelope.targetId = identity_.deviceId;
    envelope.sessionId = sessionId_;
    envelope.attemptId = attemptId_;
    envelope.sequence = snapshot_.published + 1;
    envelope.sessionNonce = nonce_;
    envelope.payloadJson = payload;
    const auto encoded = EnvelopeCodec::encode(envelope);
    if (!encoded) {
        snapshot_.lastError = "encode_failed";
        changed();
        return false;
    }
    SignalingPublish publish{*route, *encoded, policy->qos, policy->retained,
                               policy->messageExpirySeconds};
    lastCommandPublish_ = publish;
    if (trackAck) {
        pendingMessageId_ = envelope.messageId;
        pendingPublish_ = publish;
        ackTracker_.start(pendingMessageId_, clock_());
    }
    if (!channel_.publish(publish)) {
        if (trackAck) {
            ackTracker_.acknowledge(pendingMessageId_);
            pendingMessageId_.clear();
        }
        snapshot_.lastError = "publish_failed";
        changed();
        return false;
    }
    ++snapshot_.published;
    changed();
    return true;
}

bool DirectOperatorCore::replayLastCommandForValidation()
{
    if (lastCommandPublish_.payload.empty()) return false;
    const bool published = channel_.publish(lastCommandPublish_);
    if (published) {
        ++snapshot_.published;
        changed();
    }
    return published;
}

void DirectOperatorCore::poll()
{
    if (pendingMessageId_.empty()) return;
    const AckAction action = ackTracker_.poll(pendingMessageId_, clock_());
    if (action == AckAction::Retry) {
        if (channel_.publish(pendingPublish_)) ++snapshot_.published;
        changed();
    } else if (action == AckAction::TimedOut) {
        pendingMessageId_.clear();
        snapshot_.lastError = "ack_timeout";
        snapshot_.sessionState = SessionState::Failed;
        changed();
    }
}

void DirectOperatorCore::receive(const SignalingFrame &frame)
{
    ++snapshot_.received;
    if (matches(identity_, frame, TopicKind::Presence)) {
        const auto decodedPresence = EnvelopeCodec::decode(frame.payload);
        if (!decodedPresence.validation.ok || !decodedPresence.envelope
            || !validFramePolicy(frame, *decodedPresence.envelope)
            || decodedPresence.envelope->messageType != "device.presence"
            || decodedPresence.envelope->sourceKind != "device"
            || decodedPresence.envelope->sourceId != identity_.deviceId) {
            ++snapshot_.rejected;
            snapshot_.lastError = "invalid_presence";
        } else {
            const auto state = payloadString(
                decodedPresence.envelope->payloadJson, "status");
            snapshot_.deviceReady = state && *state == "online";
        }
        changed();
        return;
    }
    if (!matches(identity_, frame, TopicKind::SignalToOperator)) {
        ++snapshot_.rejected;
        snapshot_.lastError = "wrong_topic";
        changed();
        return;
    }
    const auto decoded = EnvelopeCodec::decode(frame.payload);
    if (!decoded.validation.ok || !decoded.envelope) {
        ++snapshot_.rejected;
        snapshot_.lastError = std::string(errorCode(decoded.validation.error));
        changed();
        return;
    }
    const Envelope &envelope = *decoded.envelope;
    const std::int64_t sent = parseTimestamp(envelope.sentAtUtc);
    if (!validFramePolicy(frame, envelope) || sent < 0
        || !TemporalPolicy::validate(sent, envelope.ttlMs, clock_()).ok
        || envelope.sourceKind != "device"
        || envelope.sourceId != identity_.deviceId
        || envelope.targetKind != "operator"
        || envelope.targetId != identity_.operatorId
        || envelope.sessionId != sessionId_
        || envelope.attemptId != attemptId_
        || envelope.sessionNonce != nonce_) {
        ++snapshot_.rejected;
        snapshot_.lastError = "invalid_message_context";
        changed();
        return;
    }
    const ValidationResult replay = replayGuard_.remember(
        identity_.deviceId, envelope.messageId, frame.payload.size(), clock_());
    if (!replay.ok) {
        if (replay.error == ContractError::Replay) ++snapshot_.duplicates;
        else ++snapshot_.rejected;
        changed();
        return;
    }
    if (envelope.messageType == "message.ack") {
        const auto acknowledged = payloadString(
            envelope.payloadJson, "acknowledgedMessageId");
        if (acknowledged && *acknowledged == pendingMessageId_) {
            ackTracker_.acknowledge(*acknowledged);
            pendingMessageId_.clear();
        }
    } else if (envelope.messageType == "session.accept") {
        updateState(SessionState::Accepted);
        updateState(SessionState::Negotiating);
    } else if (envelope.messageType == "session.media_state") {
        const auto state = payloadString(envelope.payloadJson, "stateOrCode");
        if (state && *state == "streaming") updateState(SessionState::Connected);
    } else if (envelope.messageType == "session.closed") {
        updateState(SessionState::Closed);
    }
    changed();
}

void DirectOperatorCore::updateState(SessionState next)
{
    if (SessionTransitionPolicy::validate(snapshot_.sessionState, next).ok)
        snapshot_.sessionState = next;
    else snapshot_.lastError = "invalid_transition";
    changed();
}

DirectCoreSnapshot DirectOperatorCore::snapshot() const { return snapshot_; }
void DirectOperatorCore::setChangedHandler(
    std::function<void(const DirectCoreSnapshot &)> handler)
{ changedHandler_ = std::move(handler); }
void DirectOperatorCore::changed()
{ if (changedHandler_) changedHandler_(snapshot_); }

DirectDeviceCore::DirectDeviceCore(ISignalingChannel &channel,
                                   DirectRouteIdentity identity,
                                   DirectClock clock,
                                   DirectIdFactory ids)
    : channel_(channel), identity_(std::move(identity)),
      clock_(clock ? std::move(clock) : DirectClock(systemNow)),
      ids_(ids ? std::move(ids) : DirectIdFactory(uuidV4))
{
    bootId_ = ids_();
    presenceId_ = ids_();
}

bool DirectDeviceCore::start()
{
    const auto route = topic(identity_, TopicKind::SignalToDevice);
    if (!route) return false;
    channel_.setMessageHandler(
        [this](const SignalingFrame &frame) { receive(frame); });
    channel_.setStateHandler([this](SignalingChannelState state,
                                    const std::string &detail) {
        snapshot_.channelState = state;
        if (state == SignalingChannelState::Ready
            && snapshot_.deviceState == DeviceAgentState::Offline) {
            snapshot_.deviceState = DeviceAgentState::Online;
            publishPresence(true);
        }
        if (state == SignalingChannelState::Error) {
            snapshot_.lastError = detail.empty() ? "channel_error" : detail;
            snapshot_.deviceState = DeviceAgentState::Faulted;
        }
        changed();
    });
    return channel_.start({*route});
}

void DirectDeviceCore::stop()
{
    if (snapshot_.channelState == SignalingChannelState::Ready)
        publishPresence(false);
    channel_.stop();
    snapshot_.deviceState = DeviceAgentState::Offline;
    changed();
}

void DirectDeviceCore::receive(const SignalingFrame &frame)
{
    ++snapshot_.received;
    if (!matches(identity_, frame, TopicKind::SignalToDevice)) {
        ++snapshot_.rejected;
        snapshot_.lastError = "wrong_topic";
        changed();
        return;
    }
    const auto decoded = EnvelopeCodec::decode(frame.payload);
    if (!decoded.validation.ok || !decoded.envelope) {
        ++snapshot_.rejected;
        snapshot_.lastError = std::string(errorCode(decoded.validation.error));
        changed();
        return;
    }
    const Envelope &request = *decoded.envelope;
    const std::int64_t sent = parseTimestamp(request.sentAtUtc);
    if (!validFramePolicy(frame, request) || sent < 0
        || !TemporalPolicy::validate(sent, request.ttlMs, clock_()).ok
        || request.sourceKind != "operator"
        || request.sourceId != identity_.operatorId
        || request.sourceClientInstanceId != identity_.clientInstanceId
        || request.targetKind != "device"
        || request.targetId != identity_.deviceId) {
        ++snapshot_.rejected;
        snapshot_.lastError = "invalid_message_context";
        changed();
        return;
    }
    const ValidationResult replay = replayGuard_.remember(
        identity_.operatorId + "/" + identity_.clientInstanceId,
        request.messageId, frame.payload.size(), clock_());
    if (!replay.ok) {
        if (replay.error == ContractError::Replay && replayCached(request.messageId))
            ++snapshot_.duplicates;
        else ++snapshot_.rejected;
        changed();
        return;
    }

    cache_[request.messageId] = {};
    const std::string ack = "{\"acknowledgedMessageId\":\""
        + request.messageId + "\",\"outcome\":\"applied\"}";
    publishReply(request, "message.ack", ack);
    if (request.messageType == "session.request") {
        if (snapshot_.deviceState != DeviceAgentState::Online) {
            publishReply(request, "session.reject",
                "{\"requestNonce\":\"" + request.sessionNonce
                + "\",\"code\":\"device_busy\",\"retryable\":false}");
        } else {
            snapshot_.deviceState = DeviceAgentState::Reserved;
            if (actionHandler_) actionHandler_(DirectAction::StartStream);
            ++snapshot_.actions;
            publishReply(request, "session.accept",
                "{\"requestNonce\":\"" + request.sessionNonce
                + "\",\"authorizationExpiresAtUtc\":\""
                + timestamp(clock_() + 60000)
                + "\",\"capabilitiesRevision\":0,\"grantedScopes\":[\"video\"]}");
            snapshot_.deviceState = DeviceAgentState::Negotiating;
            publishReply(request, "session.media_state",
                "{\"stateOrCode\":\"streaming\",\"observedAtUtc\":\""
                + timestamp(clock_()) + "\"}");
            snapshot_.deviceState = DeviceAgentState::Streaming;
        }
    } else if (request.messageType == "session.cancel") {
        if (snapshot_.deviceState == DeviceAgentState::Streaming
            || snapshot_.deviceState == DeviceAgentState::Negotiating
            || snapshot_.deviceState == DeviceAgentState::Reserved) {
            snapshot_.deviceState = DeviceAgentState::Closing;
            if (actionHandler_) actionHandler_(DirectAction::StopStream);
            ++snapshot_.actions;
            publishReply(request, "session.closed",
                         "{\"reasonCode\":\"operator_requested\"}");
            snapshot_.deviceState = DeviceAgentState::Online;
        }
    }
    changed();
}

bool DirectDeviceCore::publishReply(const Envelope &request,
                                    const std::string &type,
                                    const std::string &payload)
{
    const auto route = topic(identity_, TopicKind::SignalToOperator);
    if (!route) return false;
    const Envelope envelope = replyEnvelope(
        request, identity_, type, payload, clock_, ids_);
    SignalingPublish publish = encodedPublish(envelope, *route);
    if (publish.payload.empty() || !channel_.publish(publish)) return false;
    ++snapshot_.published;
    cache_[request.messageId].push_back(std::move(publish));
    return true;
}

bool DirectDeviceCore::publishPresence(bool online)
{
    const auto route = topic(identity_, TopicKind::Presence);
    const auto policy = policyFor("device.presence");
    if (!route || !policy) return false;
    Envelope envelope;
    envelope.messageId = ids_();
    envelope.messageType = "device.presence";
    envelope.sentAtUtc = timestamp(clock_());
    envelope.ttlMs = policy->maximumTtlMs;
    envelope.sourceKind = "device";
    envelope.sourceId = identity_.deviceId;
    envelope.sequence = ++presenceSequence_;
    envelope.payloadJson = "{\"bootId\":\"" + bootId_
        + "\",\"connectionPresenceId\":\"" + presenceId_
        + "\",\"status\":\"" + (online ? "online" : "offline")
        + "\",\"ready\":" + (online ? "true" : "false")
        + ",\"heartbeatSequence\":" + std::to_string(presenceSequence_) + "}";
    SignalingPublish publish = encodedPublish(envelope, *route);
    if (publish.payload.empty() || !channel_.publish(publish)) return false;
    ++snapshot_.published;
    return true;
}

bool DirectDeviceCore::replayCached(const std::string &messageId)
{
    const auto found = cache_.find(messageId);
    if (found == cache_.end()) return false;
    for (const SignalingPublish &reply : found->second) {
        if (channel_.publish(reply)) ++snapshot_.published;
    }
    return true;
}

DirectCoreSnapshot DirectDeviceCore::snapshot() const { return snapshot_; }
void DirectDeviceCore::setActionHandler(std::function<void(DirectAction)> handler)
{ actionHandler_ = std::move(handler); }
void DirectDeviceCore::setChangedHandler(
    std::function<void(const DirectCoreSnapshot &)> handler)
{ changedHandler_ = std::move(handler); }
void DirectDeviceCore::changed()
{ if (changedHandler_) changedHandler_(snapshot_); }

} // namespace rtmp::p2p
