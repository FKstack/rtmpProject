#include <juice/juice.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

namespace {

std::atomic_bool gStopping {false};

void requestStop(int)
{
    gStopping.store(true, std::memory_order_relaxed);
}

int timeoutFromArguments(int argc, char **argv)
{
    constexpr int kDefaultTimeoutMs = 120'000;
    if (argc == 1) return kDefaultTimeoutMs;
    if (argc != 3 || std::string(argv[1]) != "--timeout-ms") return -1;
    try {
        const long parsed = std::stol(argv[2]);
        if (parsed < 1'000 || parsed > 600'000 ||
            parsed > std::numeric_limits<int>::max()) {
            return -1;
        }
        return static_cast<int>(parsed);
    } catch (...) {
        return -1;
    }
}

} // namespace

int main(int argc, char **argv)
{
    const int timeoutMs = timeoutFromArguments(argc, argv);
    if (timeoutMs < 0) {
        std::cerr << "usage: rtmp_monitor_webrtc_stun_fixture "
                     "[--timeout-ms 1000..600000]\n";
        return 2;
    }

    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    juice_set_log_level(JUICE_LOG_LEVEL_WARN);

    juice_server_config_t configuration {};
    configuration.bind_address = "127.0.0.1";
    configuration.external_address = "127.0.0.2";
    configuration.port = 0;
    configuration.max_allocations = 0;
    configuration.max_peers = 0;
    juice_server_t *server = juice_server_create(&configuration);
    if (server == nullptr) {
        std::cerr << "{\"event\":\"stun_fixture_failed\"}\n";
        return 3;
    }

    const std::uint16_t port = juice_server_get_port(server);
    if (port == 0) {
        juice_server_destroy(server);
        std::cerr << "{\"event\":\"stun_fixture_failed\"}\n";
        return 3;
    }
    std::cout << "{\"event\":\"stun_fixture_ready\",\"port\":"
              << port << "}" << std::endl;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (!gStopping.load(std::memory_order_relaxed) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    juice_server_destroy(server);
    return 0;
}
