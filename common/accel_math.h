#pragma once

#include "sensor_data.pb.h"

#include <cmath>

namespace accel {

inline float computeAccelerationModule(const AccelPacket& packet) {
    return std::sqrt(packet.x() * packet.x() +
                     packet.y() * packet.y() +
                     packet.z() * packet.z());
}

} // namespace accel
