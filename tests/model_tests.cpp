#include "client_a/sender_client.h"
#include "client_b/processor_client.h"
#include "server/mediator.h"
#include "server/service_impl.h"

#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

class TestGrpcServer {
public:
    TestGrpcServer() {
        auto mediator = std::make_shared<accel::StreamMediator>();
        service_ = std::make_unique<accel::AccelerometerServiceImpl>(mediator);

        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port_);
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

std::filesystem::path tempFilePath(const std::string& name) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (name + "_" + std::to_string(suffix) + ".log");
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

TEST(ModelTest, EndToEndPipelineFiltersConsecutiveDuplicatesAndReturnsModulesToSender) {
    TestGrpcServer server;
    ASSERT_TRUE(server.started());

    const auto log_path = tempFilePath("accelerometer_model_test");

    accel::ProcessorClient processor(server.address());
    processor.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    accel::SenderClient sender(server.address(), log_path.string());
    std::vector<accel::AccelPacket> packets = {
            makePacket(1000, 3.0F, 4.0F, 0.0F),
            makePacket(2000, 3.0F, 4.0F, 0.0F), // duplicate by x/y/z, must be dropped
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

    std::filesystem::remove(log_path);
}
