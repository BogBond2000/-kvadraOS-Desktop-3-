#pragma once

#include "sensor_data.pb.h"

#include <mutex>

namespace accel {

class StreamPeer {
public:
    virtual bool enqueueMessage(const StreamMessage& message) = 0;
    virtual ~StreamPeer() = default;
};

class StreamMediator {
public:
    void registerSender(StreamPeer* sender);
    void registerProcessor(StreamPeer* processor);

    void unregisterSender(StreamPeer* sender);
    void unregisterProcessor(StreamPeer* processor);

    bool forwardPacketToProcessor(const AccelPacket& packet);
    bool forwardModuleToSender(const AccelModule& module);

private:
    std::mutex mu_;
    StreamPeer* sender_{nullptr};
    StreamPeer* processor_{nullptr};
};

} // namespace accel
