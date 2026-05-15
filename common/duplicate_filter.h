#pragma once
#include "sensor_data.pb.h"
#include <optional>

namespace accel {

    class DuplicateFilter {
    public:
        bool isDuplicate(const AccelPacket& packet, float eps = 1e-4f);
        void reset();
    private:
        std::optional<AccelPacket> last_;
    };

} // namespace accel