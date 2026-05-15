#include "processor_client.h"

#include "common/accel_math.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <utility>

namespace accel {
namespace {
constexpr uint32_t kProtocolVersion = 1;
constexpr auto kInitialReconnectDelay = std::chrono::milliseconds(500);
constexpr auto kMaxReconnectDelay = std::chrono::milliseconds(30000);
}

ProcessorClient::ProcessorClient(const std::string& server_addr, SecurityConfig security)
        : server_addr_(server_addr), security_(std::move(security)) {
    auto channel = grpc::CreateChannel(server_addr, makeClientCredentials(security_));
    stub_ = AccelerometerService::NewStub(channel);
}

ProcessorClient::~ProcessorClient() {
    stop();
}

void ProcessorClient::start() {
    if (running_.exchange(true)) {
        return;
    }

    worker_ = std::thread(&ProcessorClient::runLoop, this);
}

void ProcessorClient::runLoop() {
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
            std::cerr << "[processor] failed to send hello, reconnecting" << std::endl;
            stream->Finish();
            cancelActiveContext();
            std::this_thread::sleep_for(reconnect_delay);
            reconnect_delay = std::min(reconnect_delay * 2, kMaxReconnectDelay);
            continue;
        }

        std::cout << "[processor] stream connected to " << server_addr_ << std::endl;
        reconnect_delay = kInitialReconnectDelay;

        StreamMessage incoming;
        while (running_ && stream->Read(&incoming)) {
            if (incoming.payload_case() != StreamMessage::kPacket) {
                continue;
            }

            const auto& packet = incoming.packet();
            const float module = computeAccelerationModule(packet);

            AccelModule reply;
            reply.set_version(kProtocolVersion);
            reply.set_timestamp(packet.timestamp());
            reply.set_module(module);

            StreamMessage outgoing;
            outgoing.set_version(kProtocolVersion);
            *outgoing.mutable_module() = reply;

            if (!stream->Write(outgoing)) {
                std::cerr << "[processor] stream write failed" << std::endl;
                break;
            }

            std::cout << "[processor] processed: ts=" << packet.timestamp()
                      << " module=" << module << std::endl;
        }

        stream->WritesDone();
        context.TryCancel();
        grpc::Status status = stream->Finish();
        if (!status.ok() && running_) {
            std::cerr << "[processor] stream finished with error: "
                      << status.error_message() << std::endl;
        }

        cancelActiveContext();

        if (running_) {
            std::cout << "[processor] reconnecting in " << reconnect_delay.count() << " ms" << std::endl;
            std::this_thread::sleep_for(reconnect_delay);
            reconnect_delay = std::min(reconnect_delay * 2, kMaxReconnectDelay);
        }
    }
}

bool ProcessorClient::writeHello(Stream* stream) {
    StreamMessage message;
    message.set_version(kProtocolVersion);
    auto* hello = message.mutable_hello();
    hello->set_version(kProtocolVersion);
    hello->set_role(CLIENT_ROLE_PROCESSOR);
    return stream->Write(message);
}

void ProcessorClient::cancelActiveContext() {
    std::lock_guard<std::mutex> lock(context_mutex_);
    if (active_context_) {
        active_context_->TryCancel();
        active_context_ = nullptr;
    }
}

void ProcessorClient::stop() {
    if (!running_.exchange(false) && !worker_.joinable()) {
        return;
    }

    cancelActiveContext();

    if (worker_.joinable()) {
        worker_.join();
    }
}

} // namespace accel
