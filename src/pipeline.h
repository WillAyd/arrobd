#pragma once

#include <atomic>
#include <thread>

namespace obd {

class ISerial;
class WsServer;

struct PipelineOptions {
    int poll_interval_ms = 200;
};

class Pipeline {
public:
    Pipeline(ISerial& serial, WsServer& server, const PipelineOptions& opts);
    ~Pipeline();

    // Start the OBD polling thread
    void start();

    // Stop the polling thread and wait for it to join
    void stop();

private:
    void poll_loop();

    ISerial& serial_;
    WsServer& server_;
    PipelineOptions opts_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace obd
