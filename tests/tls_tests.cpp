#include "client_a/sender_client.h"
#include "client_b/processor_client.h"
#include "common/security_config.h"
#include "server/mediator.h"
#include "server/service_impl.h"

#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef ACCEL_SOURCE_DIR
#define ACCEL_SOURCE_DIR "."
#endif

namespace {

class TestGrpcServer {
public:
    TestGrpcServer(const accel::SecurityConfig& security) {
        auto mediator = std::make_shared<accel::StreamMediator>();
        service_ = std::make_unique<accel::AccelerometerServiceImpl>(mediator);
        service_->setExpectedApiKey(security.api_key);

        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", accel::makeServerCredentials(security), &selected_port_);
        builder.RegisterService(service_.get());
        server_ = builder.BuildAndStart();
        if (server_) {
            wait_thread_ = std::thread([this] { server_->Wait(); });
        }
    }

    ~TestGrpcServer() {
        if (server_) {
            server_->Shutdown();
        }
        if (wait_thread_.joinable()) {
            wait_thread_.join();
        }
    }

    std::string address() const {
        return "127.0.0.1:" + std::to_string(selected_port_);
    }

    bool started() const {
        return server_ != nullptr && selected_port_ > 0;
    }

private:
    int selected_port_{0};
    std::unique_ptr<accel::AccelerometerServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
    std::thread wait_thread_;
};

accel::AccelPacket makePacket(int64_t timestamp, float x, float y, float z) {
    accel::AccelPacket packet;
    packet.set_version(1);
    packet.set_timestamp(timestamp);
    packet.set_x(x);
    packet.set_y(y);
    packet.set_z(z);
    return packet;
}

std::filesystem::path uniqueTempDir(const std::string& name) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() / (name + "_" + std::to_string(suffix));
    std::filesystem::create_directories(path);
    return path;
}

void generateCertsOrSkip(const std::filesystem::path& dir) {
    const std::filesystem::path script = std::filesystem::path(ACCEL_SOURCE_DIR) / "scripts" / "generate_certs.sh";
    const std::string command = "bash \"" + script.string() + "\" \"" + dir.string() + "\"";
    const int result = std::system(command.c_str());
    if (result != 0) {
        GTEST_SKIP() << "openssl/bash certificate generation failed";
    }
}

accel::SecurityConfig makeTlsConfig(const std::filesystem::path& dir,
                                    bool server_side,
                                    bool mtls,
                                    std::string api_key) {
    accel::SecurityConfig config;
    config.use_tls = true;
    config.require_client_cert = server_side && mtls;
    config.api_key = std::move(api_key);
    config.ca_cert_path = (dir / "ca.crt").string();
    if (server_side) {
        config.server_cert_path = (dir / "server.crt").string();
        config.server_key_path = (dir / "server.key").string();
    } else if (mtls) {
        config.client_cert_path = (dir / "client.crt").string();
        config.client_key_path = (dir / "client.key").string();
    }
    return config;
}

std::vector<std::pair<int64_t, float>> readModuleLog(const std::filesystem::path& path) {
    std::vector<std::pair<int64_t, float>> rows;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream parsed(line);
        int64_t timestamp = 0;
        float module = 0.0F;
        if (parsed >> timestamp >> module) {
            rows.emplace_back(timestamp, module);
        }
    }
    return rows;
}

bool waitForLogRows(const std::filesystem::path& path,
                    std::size_t expected_rows,
                    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (readModuleLog(path).size() >= expected_rows) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

} // namespace

TEST(TlsModelTest, EndToEndPipelineWorksWithMtlsAndApiKey) {
    const auto cert_dir = uniqueTempDir("accelerometer_tls_certs");
    generateCertsOrSkip(cert_dir);

    constexpr const char* kApiKey = "test-api-key-0123456789abcdef";
    TestGrpcServer server(makeTlsConfig(cert_dir, true, true, kApiKey));
    ASSERT_TRUE(server.started());

    auto client_security = makeTlsConfig(cert_dir, false, true, kApiKey);
    const auto log_path = cert_dir / "accel_module.log";

    accel::ProcessorClient processor(server.address(), client_security);
    processor.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    accel::SenderClient sender(server.address(), log_path.string(), client_security);
    std::vector<accel::AccelPacket> packets = {
            makePacket(1000, 3.0F, 4.0F, 0.0F),
            makePacket(2000, 3.0F, 4.0F, 0.0F),
            makePacket(3000, 0.0F, 0.0F, 12.0F),
    };
    std::size_t index = 0;

    sender.start([&packets, &index](accel::AccelPacket& packet) {
        if (index >= packets.size()) {
            return false;
        }
        packet = packets[index++];
        return true;
    });

    ASSERT_TRUE(waitForLogRows(log_path, 2, std::chrono::seconds(5)));

    sender.stop();
    processor.stop();

    const auto rows = readModuleLog(log_path);
    ASSERT_GE(rows.size(), 2U);
    EXPECT_EQ(rows[0].first, 1000);
    EXPECT_FLOAT_EQ(rows[0].second, 5.0F);
    EXPECT_EQ(rows[1].first, 3000);
    EXPECT_FLOAT_EQ(rows[1].second, 12.0F);

    std::filesystem::remove_all(cert_dir);
}

TEST(TlsModelTest, RejectsInvalidApiKeyOverTls) {
    const auto cert_dir = uniqueTempDir("accelerometer_tls_reject_certs");
    generateCertsOrSkip(cert_dir);

    TestGrpcServer server(makeTlsConfig(cert_dir, true, false, "expected-key"));
    ASSERT_TRUE(server.started());

    auto client_security = makeTlsConfig(cert_dir, false, false, "wrong-key");
    auto channel = grpc::CreateChannel(server.address(), accel::makeClientCredentials(client_security));
    auto stub = accel::AccelerometerService::NewStub(channel);

    grpc::ClientContext context;
    context.AddMetadata("x-api-key", client_security.api_key);
    auto stream = stub->StreamAccelData(&context);

    accel::StreamMessage hello;
    hello.set_version(1);
    hello.mutable_hello()->set_version(1);
    hello.mutable_hello()->set_role(accel::CLIENT_ROLE_SENDER);
    stream->Write(hello);
    stream->WritesDone();

    const grpc::Status status = stream->Finish();
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAUTHENTICATED);

    std::filesystem::remove_all(cert_dir);
}
