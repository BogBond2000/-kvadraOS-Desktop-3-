#pragma once

#include "common/module_logger.h"
#include "common/security_config.h"
#include "sensor_data.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace accel {

class SenderClient {
public:
    explicit SenderClient(const std::string& server_addr,
                          std::string log_path = "accel_module.log",
                          SecurityConfig security = {});
    ~SenderClient();

    void start(std::function<bool(AccelPacket&)> data_callback);
    void stop();

private:
    using Stream = grpc::ClientReaderWriter<StreamMessage, StreamMessage>;

    void runLoop(std::function<bool(AccelPacket&)> data_callback);
    void readLoop(Stream* stream, std::atomic<bool>& session_alive);
    bool writeHello(Stream* stream);
    void cancelActiveContext();

    std::string server_addr_;
    SecurityConfig security_;
    std::unique_ptr<AccelerometerService::Stub> stub_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    std::mutex context_mutex_;
    grpc::ClientContext* active_context_{nullptr};

    ModuleLogger logger_;
};

} // namespace accel
