#include "processor_client.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> g_running{true};

void handleSignal(int) {
    g_running = false;
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [server_addr] [options]\n"
              << "Options:\n"
              << "  --api-key KEY\n"
              << "  --tls CA [CLIENT_CERT CLIENT_KEY]\n";
}
} // namespace

int main(int argc, char** argv) {
    const std::string server_addr = argc > 1 ? argv[1] : "127.0.0.1:50051";

    accel::SecurityConfig security;
    int index = 2;
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

    accel::ProcessorClient client(server_addr, security);
    client.start();

    std::cout << "[processor] connected to " << server_addr << std::endl;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    client.stop();
    return 0;
}
