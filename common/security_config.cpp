#include "common/security_config.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace accel {

std::string readFileOrThrow(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open file: " + path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::shared_ptr<grpc::ChannelCredentials> makeClientCredentials(const SecurityConfig& config) {
    if (!config.use_tls) {
        return grpc::InsecureChannelCredentials();
    }

    grpc::SslCredentialsOptions options;
    if (!config.ca_cert_path.empty()) {
        options.pem_root_certs = readFileOrThrow(config.ca_cert_path);
    }
    if (!config.client_key_path.empty()) {
        options.pem_private_key = readFileOrThrow(config.client_key_path);
    }
    if (!config.client_cert_path.empty()) {
        options.pem_cert_chain = readFileOrThrow(config.client_cert_path);
    }

    return grpc::SslCredentials(options);
}

std::shared_ptr<grpc::ServerCredentials> makeServerCredentials(const SecurityConfig& config) {
    if (!config.use_tls) {
        return grpc::InsecureServerCredentials();
    }

    grpc::SslServerCredentialsOptions options;
    options.pem_root_certs = config.ca_cert_path.empty() ? std::string{} : readFileOrThrow(config.ca_cert_path);
    options.pem_key_cert_pairs.push_back({readFileOrThrow(config.server_key_path),
                                          readFileOrThrow(config.server_cert_path)});
    options.client_certificate_request = config.require_client_cert
            ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
            : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;

    return grpc::SslServerCredentials(options);
}

} // namespace accel
