#include "module_logger.h"

namespace accel {

    bool ModuleLogger::open(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        file_.open(path, std::ios::out | std::ios::app);
        return file_.is_open();
    }

    void ModuleLogger::log(int64_t timestamp, float module) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_ << timestamp << " " << module << std::endl;
        }
    }

    void ModuleLogger::close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) file_.close();
    }

} // namespace accel