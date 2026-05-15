#include "sender_client.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <utility>

namespace accel {
namespace {
constexpr uint32_t kProtocolVersion = 1;
constexpr auto kSamplePeriod = std::chrono::milliseconds(20);
constexpr auto kInitialReconnectDelay = std::chrono::milliseconds(500);
constexpr auto kMaxReconnectDelay = std::chrono::milliseconds(30000);
}

SenderClient::SenderClient(const std::string& server_addr, std::string log_path, SecurityConfig security)
        : server_addr_(server_addr), security_(std::move(security)) {
    auto channel = grpc::CreateChannel(server_addr, makeClientCredentials(security_));
    stub_ = AccelerometerService::NewStub(channel);
    if (!logger_.open(log_path)) {
        std::cerr << "[sender] failed to open module log: " << log_path << std::endl;
    }
}

SenderClient::~SenderClient() {
    stop();
    logger_.close();
}

void SenderClient::start(std::function<bool(AccelPacket&)> data_callback) {
    if (running_.exchange(true)) {
        return;
    }

    worker_thread_ = std::thread(&SenderClient::runLoop, this, std::move(data_callback));
}

void SenderClient::runLoop(std::function<bool(AccelPacket&)> data_callback) {
    auto reconnect_delay = kInitialReconnectDelay;

    while (running_) {
        grpc::ClientContext context;
        if (!security_.api_key.empty()) {
            context.AddMetadata("x-api-key", security_.api_key);
        }
        {
            std::lock_guard<std::mutex> lock(context_mutex_);
            active_context_ = &context;
        }

        auto stream = stub_->StreamAccelData(&context);
        if (!writeHello(stream.get())) {
            std::cerr << "[sender] failed to send hello, reconnecting" << std::endl;
            stream->Finish();
            cancelActiveContext();
            std::this_thread::sleep_for(reconnect_delay);
            reconnect_delay = std::min(reconnect_delay * 2, kMaxReconnectDelay);
            continue;
        }

        std::cout << "[sender] stream connected to " << server_addr_ << std::endl;
        reconnect_delay = kInitialReconnectDelay;

        std::atomic<bool> session_alive{true};
        std::thread reader(&SenderClient::readLoop, this, stream.get(), std::ref(session_alive));

        while (running_ && session_alive) {
            AccelPacket packet;
            if (data_callback(packet)) {
                packet.set_version(kProtocolVersion);

                StreamMessage message;
                message.set_version(kProtocolVersion);
                *message.mutable_packet() = packet;

                if (!stream->Write(message)) {
                    std::cerr << "[sender] stream write failed" << std::endl;
                    session_alive = false;
                    break;
                }
                std::cout << "[sender] sent packet: ts=" << packet.timestamp() << std::endl;
            }
            std::this_thread::sleep_for(kSamplePeriod);
        }

        stream->WritesDone();
        context.TryCancel();
        if (reader.joinable()) {
            reader.join();
        }

        grpc::Status status = stream->Finish();
        if (!status.ok() && running_) {
            std::cerr << "[sender] stream finished with error: "
                      << status.error_message() << std::endl;
        }

        cancelActiveContext();

        if (running_) {
            std::cout << "[sender] reconnecting in " << reconnect_delay.count() << " ms" << std::endl;
            std::this_thread::sleep_for(reconnect_delay);
            reconnect_delay = std::min(reconnect_delay * 2, kMaxReconnectDelay);
        }
    }
}

bool SenderClient::writeHello(Stream* stream) {
    StreamMessage message;
    message.set_version(kProtocolVersion);
    auto* hello = message.mutable_hello();
    hello->set_version(kProtocolVersion);
    hello->set_role(CLIENT_ROLE_SENDER);
    return stream->Write(message);
}

void SenderClient::readLoop(Stream* stream, std::atomic<bool>& session_alive) {
    StreamMessage message;
    while (running_ && session_alive && stream->Read(&message)) {
        if (message.payload_case() != StreamMessage::kModule) {
            continue;
        }

        const auto& module = message.module();
        logger_.log(module.timestamp(), module.module());
        std::cout << "[sender] module received: ts=" << module.timestamp()
                  << " module=" << module.module() << std::endl;
    }
    session_alive = false;
}

void SenderClient::cancelActiveContext() {
    std::lock_guard<std::mutex> lock(context_mutex_);
    if (active_context_) {
        active_context_->TryCancel();
        active_context_ = nullptr;
    }
}

void SenderClient::stop() {
    if (!running_.exchange(false) && !worker_thread_.joinable()) {
        return;
    }

    cancelActiveContext();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

} // namespace accel
