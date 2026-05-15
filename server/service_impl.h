#pragma once

#include "common/duplicate_filter.h"
#include "mediator.h"
#include "sensor_data.grpc.pb.h"

#include <memory>
#include <mutex>
#include <string>

namespace accel {

class AccelerometerServiceImpl final : public AccelerometerService::CallbackService {
public:
    explicit AccelerometerServiceImpl(std::shared_ptr<StreamMediator> mediator);

    void setExpectedApiKey(std::string api_key);

    grpc::ServerBidiReactor<StreamMessage, StreamMessage>* StreamAccelData(
            grpc::CallbackServerContext* context) override;

private:
    std::shared_ptr<StreamMediator> mediator_;
    DuplicateFilter filter_;
    std::mutex filter_mutex_;
    std::string expected_api_key_;
};

} // namespace accel
