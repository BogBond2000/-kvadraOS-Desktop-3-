#include "common/accel_math.h"
#include "common/duplicate_filter.h"
#include "common/module_logger.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

accel::AccelPacket makePacket(int64_t timestamp, float x, float y, float z) {
    accel::AccelPacket packet;
    packet.set_version(1);
    packet.set_timestamp(timestamp);
    packet.set_x(x);
    packet.set_y(y);
    packet.set_z(z);
    return packet;
}

std::filesystem::path tempFilePath(const std::string& name) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (name + "_" + std::to_string(suffix) + ".log");
}

} // namespace

TEST(DuplicateFilterTest, FirstPacketIsNotDuplicate) {
    accel::DuplicateFilter filter;
    EXPECT_FALSE(filter.isDuplicate(makePacket(1000, 1.0F, 2.0F, 3.0F)));
}

TEST(DuplicateFilterTest, ConsecutiveEqualValuesAreDuplicateEvenWithDifferentTimestamp) {
    accel::DuplicateFilter filter;
    EXPECT_FALSE(filter.isDuplicate(makePacket(1000, 1.0F, 2.0F, 3.0F)));
    EXPECT_TRUE(filter.isDuplicate(makePacket(2000, 1.0F, 2.0F, 3.0F)));
}

TEST(DuplicateFilterTest, ValuesWithinEpsilonAreDuplicate) {
    accel::DuplicateFilter filter;
    EXPECT_FALSE(filter.isDuplicate(makePacket(1000, 1.0F, 2.0F, 3.0F)));
    EXPECT_TRUE(filter.isDuplicate(makePacket(2000, 1.00005F, 2.00005F, 3.00005F)));
}

TEST(DuplicateFilterTest, ChangedValueUpdatesLastAcceptedPacket) {
    accel::DuplicateFilter filter;
    EXPECT_FALSE(filter.isDuplicate(makePacket(1000, 1.0F, 2.0F, 3.0F)));
    EXPECT_TRUE(filter.isDuplicate(makePacket(2000, 1.0F, 2.0F, 3.0F)));
    EXPECT_FALSE(filter.isDuplicate(makePacket(3000, 1.0F, 2.0F, 3.001F)));
    EXPECT_TRUE(filter.isDuplicate(makePacket(4000, 1.0F, 2.0F, 3.001F)));
}

TEST(DuplicateFilterTest, ResetClearsLastPacket) {
    accel::DuplicateFilter filter;
    EXPECT_FALSE(filter.isDuplicate(makePacket(1000, 1.0F, 2.0F, 3.0F)));
    EXPECT_TRUE(filter.isDuplicate(makePacket(2000, 1.0F, 2.0F, 3.0F)));

    filter.reset();

    EXPECT_FALSE(filter.isDuplicate(makePacket(3000, 1.0F, 2.0F, 3.0F)));
}

TEST(AccelMathTest, ComputesVectorModule) {
    const auto packet = makePacket(1000, 3.0F, 4.0F, 12.0F);
    EXPECT_FLOAT_EQ(accel::computeAccelerationModule(packet), 13.0F);
}

TEST(ModuleLoggerTest, WritesTimestampAndModule) {
    const auto path = tempFilePath("module_logger_test");

    accel::ModuleLogger logger;
    ASSERT_TRUE(logger.open(path.string()));
    logger.log(1712938123456LL, 9.808F);
    logger.close();

    std::ifstream input(path);
    ASSERT_TRUE(input.is_open());

    std::string line;
    std::getline(input, line);

    std::istringstream parsed(line);
    int64_t timestamp = 0;
    float module = 0.0F;
    parsed >> timestamp >> module;

    EXPECT_EQ(timestamp, 1712938123456LL);
    EXPECT_FLOAT_EQ(module, 9.808F);

    std::filesystem::remove(path);
}
