#include <android/sensor.h>
#include <android/looper.h>
#include <chrono>
#include <thread>
#include "sensor_data.pb.h"

class AndroidSensorSource {
public:
    bool start(ASensorManager* manager, ALooper* looper, std::function<void(AccelPacket)> callback) {
        // Получение датчика акселерометра
        ASensorRef accel = ASensorManager_getDefaultSensor(manager, ASENSOR_TYPE_ACCELEROMETER);
        if (!accel) return false;
        ASensorEventQueue* queue = ASensorManager_createEventQueue(manager, looper, LOOPER_ID_USER, nullptr, nullptr);
        ASensorEventQueue_enableSensor(queue, accel);
        ASensorEventQueue_setEventRate(queue, accel, 20000); // 20 ms (50 Hz)

        std::thread([this, queue, callback]() {
            ALooper* looper = ALooper_forThread();
            while (running_) {
                int fd = ASensorEventQueue_getFd(queue);
                ALooper_pollAll(20, nullptr, nullptr, nullptr);
                ASensorEvent event;
                while (ASensorEventQueue_getEvents(queue, &event, 1) > 0) {
                    AccelPacket packet;
                    packet.set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                    packet.set_x(event.acceleration.x);
                    packet.set_y(event.acceleration.y);
                    packet.set_z(event.acceleration.z);
                    callback(packet);
                }
            }
        }).detach();
        return true;
    }
    void stop() { running_ = false; }
private:
    std::atomic<bool> running_{true};
};