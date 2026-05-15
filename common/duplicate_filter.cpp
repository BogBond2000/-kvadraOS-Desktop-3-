#include "duplicate_filter.h"
#include <cmath>

namespace accel {

    bool DuplicateFilter::isDuplicate(const AccelPacket& packet, float eps) {
        if (!last_.has_value()) {
            last_ = packet;
            return false;
        }
        bool dup = (std::fabs(last_->x() - packet.x()) <= eps &&
                    std::fabs(last_->y() - packet.y()) <= eps &&
                    std::fabs(last_->z() - packet.z()) <= eps);
        if (!dup) {
            last_ = packet;
        }
        return dup;
    }

    void DuplicateFilter::reset() {
        last_.reset();
    }

} // namespace accel