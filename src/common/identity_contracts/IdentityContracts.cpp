#include "identity_contracts/IdentityContracts.h"

namespace rtmp::p2p {
namespace {
const char *suffix(MqttPlane plane)
{
    return plane == MqttPlane::Signal ? "signal" : "control";
}
}

std::string MqttClientIdCodec::device(const DeviceId &deviceId, MqttPlane plane)
{
    return "device-" + deviceId.value() + "-" + suffix(plane);
}

std::string MqttClientIdCodec::operatorClient(
    const UserId &userId, const ClientInstanceId &instanceId, MqttPlane plane)
{
    return "operator-" + userId.value() + "-" + instanceId.value() + "-"
        + suffix(plane);
}

} // namespace rtmp::p2p
