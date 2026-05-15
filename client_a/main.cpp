#include "sender_client.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> g_running{true};

void handleSignal(int) {
    g_running = false;
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [server_addr] [log_path] [options]\n"
              << "Options:\n"
              << "  --api-key KEY\n"
              << "  --tls CA [CLIENT_CERT CLIENT_KEY]\n";
}
} // namespace

int main(int argc, char** argv) {
    const std::string server_addr = argc > 1 ? argv[1] : "127.0.0.1:50051";
    const std::string log_path = argc > 2 && std::string(argv[2]).rfind("--", 0) != 0
            ? argv[2]
            : "accel_module.log";
    int index = log_path == "accel_module.log" ? 2 : 3;

    accel::SecurityConfig security;
    while (index < argc) {
        const std::string arg = argv[index++];
        if (arg == "--api-key" && index < argc) {
            security.api_key = argv[index++];
        } else if (arg == "--tls" && index < argc) {
            security.use_tls = true;
            security.ca_cert_path = argv[index++];
            if (index + 1 < argc && std::string(argv[index]).rfind("--", 0) != 0) {
                security.client_cert_path = argv[index++];
                security.client_key_path = argv[index++];
            }
        } else if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            printUsage(argv[0]);
            return 1;
        }
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    accel::SenderClient client(server_addr, log_path, security);

    float t = 0.0F;
    client.start([&t](accel::AccelPacket& packet) {
        packet.set_timestamp(nowMs());
        packet.set_x(std::sin(t));
        packet.set_y(9.81F + 0.05F * std::sin(t * 0.5F));
        packet.set_z(0.2F * std::cos(t));
        t += 0.02F;
        return true;
    });

    std::cout << "[sender] connected to " << server_addr
              << ", writing modules to " << log_path << std::endl;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    client.stop();
    return 0;
}
