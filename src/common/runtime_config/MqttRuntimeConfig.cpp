#include "runtime_config/MqttRuntimeConfig.h"

#include "identity_contracts/IdentityContracts.h"

#include <unordered_set>

namespace rtmp::p2p {

ConfigValidation validateMqttRuntimeConfig(const MqttRuntimeConfig &config)
{
    if (config.schemaVersion != 1 || config.protocolVersion != 5
        || config.topicRoot != "rtmp-monitor/v1") {
        return {false, "unsupported_config_version"};
    }
    if (!config.enabled) {
        if (!config.brokerHostname.empty() || config.brokerPort != 0
            || !config.caReference.empty()
            || !config.signalCredentialReference.empty()
            || !config.controlCredentialReference.empty()) {
            return {false, "disabled_config_contains_endpoint_or_credential"};
        }
    } else if (config.brokerHostname.empty() || config.brokerPort < 1
               || config.brokerPort > 65535 || config.signalClientId.empty()
               || config.signalCredentialReference.empty()) {
        return {false, "enabled_config_incomplete"};
    }

    std::unordered_set<std::string> devices;
    bool hasControlScope = false;
    for (const auto &entry : config.authorizedDevices) {
        if (!DeviceId::parse(entry.deviceId)
            || !MqttControlTargetId::parse(entry.controlTargetId)
            || !devices.insert(entry.deviceId).second) {
            return {false, "invalid_authorized_device"};
        }
        hasControlScope = hasControlScope || entry.scope == DeviceScope::Control;
    }
    if (!hasControlScope && !config.authorizedDevices.empty()
        && (!config.controlClientId.empty()
            || !config.controlCredentialReference.empty())) {
        return {false, "view_scope_has_control_credential"};
    }
    return {true, {}};
}

} // namespace rtmp::p2p
