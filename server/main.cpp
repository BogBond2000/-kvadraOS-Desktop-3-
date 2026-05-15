#include <grpcpp/grpcpp.h>

#include "common/security_config.h"
#include "service_impl.h"

#include <iostream>
#include <memory>
#include <string>

namespace {
void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [address] [options]\n"
              << "Options:\n"
              << "  --api-key KEY                         require application API key\n"
              << "  --tls CA SERVER_CERT SERVER_KEY       enable one-way TLS\n"
              << "  --mtls CA SERVER_CERT SERVER_KEY      enable mutual TLS\n";
}
} // namespace

int main(int argc, char** argv) {
    std::string server_address = "0.0.0.0:50051";
    accel::SecurityConfig security;

    int index = 1;
    if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
        server_address = argv[index++];
    }

    while (index < argc) {
        const std::string arg = argv[index++];
        if (arg == "--api-key" && index < argc) {
            security.api_key = argv[index++];
        } else if ((arg == "--tls" || arg == "--mtls") && index + 2 < argc) {
            security.use_tls = true;
            security.require_client_cert = arg == "--mtls";
            security.ca_cert_path = argv[index++];
            security.server_cert_path = argv[index++];
            security.server_key_path = argv[index++];
        } else if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            printUsage(argv[0]);
            return 1;
        }
    }

    auto mediator = std::make_shared<accel::StreamMediator>();
    accel::AccelerometerServiceImpl service(mediator);
    service.setExpectedApiKey(security.api_key);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, accel::makeServerCredentials(security));
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    if (!server) {
        std::cerr << "Failed to start server on " << server_address << std::endl;
        return 1;
    }

    std::cout << "Server listening on " << server_address
              << (security.use_tls ? " with TLS" : " without TLS") << std::endl;
    server->Wait();
    return 0;
}
