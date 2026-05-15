#pragma once
#include <fstream>
#include <string>
#include <mutex>

namespace accel {

    class ModuleLogger {
    public:
        bool open(const std::string& path);
        void log(int64_t timestamp, float module);
        void close();
    private:
        std::ofstream file_;
        std::mutex mutex_;
    };

} // namespace accel