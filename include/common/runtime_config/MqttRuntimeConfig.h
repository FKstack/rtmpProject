#pragma once

#include <string>
#include <vector>

namespace rtmp::p2p {

enum class DeviceScope { View, Control };

struct AuthorizedDevice final {
    std::string deviceId;
    std::string controlTargetId;
    DeviceScope scope{DeviceScope::View};
};

struct MqttRuntimeConfig final {
    int schemaVersion{1};
    bool enabled{false};
    std::string transport{"tcp"};
    std::string authMode{"anonymous"};
    std::string brokerHostname;
    int brokerPort{0};
    int protocolVersion{5};
    std::string topicRoot{"rtmp-monitor/v1"};
    std::string signalClientId;
    std::string controlClientId;
    std::string caReference;
    std::string signalCredentialReference;
    std::string controlCredentialReference;
    std::vector<AuthorizedDevice> authorizedDevices;
};

struct ConfigValidation final {
    bool ok{false};
    std::string error;
};

ConfigValidation validateMqttRuntimeConfig(const MqttRuntimeConfig &config);

} // namespace rtmp::p2p
