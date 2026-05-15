#pragma once

#include "common/security_config.h"
#include "sensor_data.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace accel {

class ProcessorClient {
public:
    explicit ProcessorClient(const std::string& server_addr, SecurityConfig security = {});
    ~ProcessorClient();

    void start();
    void stop();

private:
    using Stream = grpc::ClientReaderWriter<StreamMessage, StreamMessage>;

    void runLoop();
    bool writeHello(Stream* stream);
    void cancelActiveContext();

    std::string server_addr_;
    SecurityConfig security_;
    std::unique_ptr<AccelerometerService::Stub> stub_;
    std::thread worker_;
    std::atomic<bool> running_{false};

    std::mutex context_mutex_;
    grpc::ClientContext* active_context_{nullptr};
};

} // namespace accel
