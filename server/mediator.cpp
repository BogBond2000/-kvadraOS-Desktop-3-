#include "mediator.h"

#include <iostream>

namespace accel {
namespace {
constexpr uint32_t kProtocolVersion = 1;
}

void StreamMediator::registerSender(StreamPeer* sender) {
    std::lock_guard<std::mutex> lock(mu_);
    sender_ = sender;
    std::cout << "[server] sender connected" << std::endl;
}

void StreamMediator::registerProcessor(StreamPeer* processor) {
    std::lock_guard<std::mutex> lock(mu_);
    processor_ = processor;
    std::cout << "[server] processor connected" << std::endl;
}

void StreamMediator::unregisterSender(StreamPeer* sender) {
    std::lock_guard<std::mutex> lock(mu_);
    if (sender_ == sender) {
        sender_ = nullptr;
        std::cout << "[server] sender disconnected" << std::endl;
    }
}

void StreamMediator::unregisterProcessor(StreamPeer* processor) {
    std::lock_guard<std::mutex> lock(mu_);
    if (processor_ == processor) {
        processor_ = nullptr;
        std::cout << "[server] processor disconnected" << std::endl;
    }
}

bool StreamMediator::forwardPacketToProcessor(const AccelPacket& packet) {
    StreamPeer* processor = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu_);
        processor = processor_;
    }

    if (!processor) {
        std::cout << "[server] processor is not connected; packet dropped: "
                  << packet.timestamp() << std::endl;
        return false;
    }

    StreamMessage message;
    message.set_version(kProtocolVersion);
    *message.mutable_packet() = packet;
    return processor->enqueueMessage(message);
}

bool StreamMediator::forwardModuleToSender(const AccelModule& module) {
    StreamPeer* sender = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu_);
        sender = sender_;
    }

    if (!sender) {
        std::cout << "[server] sender is not connected; module dropped: "
                  << module.timestamp() << std::endl;
        return false;
    }

    StreamMessage message;
    message.set_version(kProtocolVersion);
    *message.mutable_module() = module;
    return sender->enqueueMessage(message);
}

} // namespace accel
