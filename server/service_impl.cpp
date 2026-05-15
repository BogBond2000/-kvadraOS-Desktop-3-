#include "service_impl.h"

#include <deque>
#include <iostream>
#include <utility>

namespace accel {
namespace {
constexpr uint32_t kSupportedProtocolVersion = 1;
constexpr const char* kApiKeyMetadataName = "x-api-key";

class RejectedReactor final : public grpc::ServerBidiReactor<StreamMessage, StreamMessage> {
public:
    explicit RejectedReactor(grpc::Status status) {
        Finish(std::move(status));
    }

    void OnDone() override {
        delete this;
    }
};

class StreamReactor final : public grpc::ServerBidiReactor<StreamMessage, StreamMessage>,
                            public StreamPeer {
public:
    StreamReactor(std::shared_ptr<StreamMediator> mediator,
                  DuplicateFilter& filter,
                  std::mutex& filter_mutex)
            : mediator_(std::move(mediator)),
              filter_(filter),
              filter_mutex_(filter_mutex) {
        StartRead(&incoming_);
    }

    bool enqueueMessage(const StreamMessage& message) override {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (finishing_) {
            return false;
        }

        write_queue_.push_back(message);
        if (!write_in_progress_) {
            write_in_progress_ = true;
            StartWrite(&write_queue_.front());
        }
        return true;
    }

    void OnReadDone(bool ok) override {
        if (!ok) {
            unregister();
            finishOnce(grpc::Status::OK);
            return;
        }

        handleMessage(incoming_);
        incoming_.Clear();

        if (!isFinishing()) {
            StartRead(&incoming_);
        }
    }

    void OnWriteDone(bool ok) override {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (!ok) {
            finishing_ = true;
            return;
        }

        if (!write_queue_.empty()) {
            write_queue_.pop_front();
        }

        if (!write_queue_.empty()) {
            StartWrite(&write_queue_.front());
        } else {
            write_in_progress_ = false;
        }
    }

    void OnDone() override {
        unregister();
        delete this;
    }

private:
    void handleMessage(const StreamMessage& message) {
        if (message.version() != kSupportedProtocolVersion) {
            std::cout << "[server] unsupported stream message version: "
                      << message.version() << std::endl;
            return;
        }

        switch (message.payload_case()) {
            case StreamMessage::kHello:
                handleHello(message.hello());
                break;
            case StreamMessage::kPacket:
                handlePacket(message.packet());
                break;
            case StreamMessage::kModule:
                handleModule(message.module());
                break;
            case StreamMessage::PAYLOAD_NOT_SET:
                std::cout << "[server] empty stream message ignored" << std::endl;
                break;
        }
    }

    void handleHello(const ClientHello& hello) {
        if (hello.version() != kSupportedProtocolVersion) {
            std::cout << "[server] unsupported hello version: " << hello.version() << std::endl;
            return;
        }

        unregister();
        role_ = hello.role();
        if (role_ == CLIENT_ROLE_SENDER) {
            mediator_->registerSender(this);
        } else if (role_ == CLIENT_ROLE_PROCESSOR) {
            mediator_->registerProcessor(this);
        } else {
            std::cout << "[server] unknown client role" << std::endl;
            finishOnce(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "unknown client role"));
        }
    }

    void handlePacket(const AccelPacket& packet) {
        if (role_ != CLIENT_ROLE_SENDER) {
            std::cout << "[server] packet received from non-sender stream" << std::endl;
            return;
        }
        if (packet.version() != kSupportedProtocolVersion) {
            std::cout << "[server] unsupported packet version: " << packet.version() << std::endl;
            return;
        }

        bool duplicate = false;
        {
            std::lock_guard<std::mutex> lock(filter_mutex_);
            duplicate = filter_.isDuplicate(packet);
        }

        if (duplicate) {
            std::cout << "[server] duplicate dropped: ts=" << packet.timestamp()
                      << " x=" << packet.x()
                      << " y=" << packet.y()
                      << " z=" << packet.z() << std::endl;
            return;
        }

        std::cout << "[server] forwarding packet to processor: ts=" << packet.timestamp() << std::endl;
        mediator_->forwardPacketToProcessor(packet);
    }

    void handleModule(const AccelModule& module) {
        if (role_ != CLIENT_ROLE_PROCESSOR) {
            std::cout << "[server] module received from non-processor stream" << std::endl;
            return;
        }
        if (module.version() != kSupportedProtocolVersion) {
            std::cout << "[server] unsupported module version: " << module.version() << std::endl;
            return;
        }

        std::cout << "[server] forwarding module to sender: ts=" << module.timestamp()
                  << " module=" << module.module() << std::endl;
        mediator_->forwardModuleToSender(module);
    }

    void unregister() {
        if (role_ == CLIENT_ROLE_SENDER) {
            mediator_->unregisterSender(this);
        } else if (role_ == CLIENT_ROLE_PROCESSOR) {
            mediator_->unregisterProcessor(this);
        }
        role_ = CLIENT_ROLE_UNKNOWN;
    }

    void finishOnce(const grpc::Status& status) {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (finishing_) {
            return;
        }
        finishing_ = true;
        Finish(status);
    }

    bool isFinishing() const {
        std::lock_guard<std::mutex> lock(write_mutex_);
        return finishing_;
    }

    std::shared_ptr<StreamMediator> mediator_;
    DuplicateFilter& filter_;
    std::mutex& filter_mutex_;
    StreamMessage incoming_;
    ClientRole role_{CLIENT_ROLE_UNKNOWN};

    mutable std::mutex write_mutex_;
    std::deque<StreamMessage> write_queue_;
    bool write_in_progress_{false};
    bool finishing_{false};
};
} // namespace

AccelerometerServiceImpl::AccelerometerServiceImpl(std::shared_ptr<StreamMediator> mediator)
        : mediator_(std::move(mediator)) {}

void AccelerometerServiceImpl::setExpectedApiKey(std::string api_key) {
    expected_api_key_ = std::move(api_key);
}

grpc::ServerBidiReactor<StreamMessage, StreamMessage>* AccelerometerServiceImpl::StreamAccelData(
        grpc::CallbackServerContext* context) {
    if (!expected_api_key_.empty()) {
        const auto& metadata = context->client_metadata();
        const auto it = metadata.find(kApiKeyMetadataName);
        if (it == metadata.end() || std::string(it->second.data(), it->second.length()) != expected_api_key_) {
            std::cout << "[security] rejected stream: invalid API key" << std::endl;
            return new RejectedReactor(grpc::Status(grpc::StatusCode::UNAUTHENTICATED,
                                                   "invalid API key"));
        }
    }

    return new StreamReactor(mediator_, filter_, filter_mutex_);
}

} // namespace accel
