#pragma once

#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

namespace accel {

struct SecurityConfig {
    bool use_tls{false};
    bool require_client_cert{false};

    std::string ca_cert_path;
    std::string server_cert_path;
    std::string server_key_path;
    std::string client_cert_path;
    std::string client_key_path;
    std::string api_key;
};

std::string readFileOrThrow(const std::string& path);

std::shared_ptr<grpc::ChannelCredentials> makeClientCredentials(const SecurityConfig& config);
std::shared_ptr<grpc::ServerCredentials> makeServerCredentials(const SecurityConfig& config);

} // namespace accel
