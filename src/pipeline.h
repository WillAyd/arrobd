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

    void start();
    void stop();
    void set_poll_interval(int ms);

private:
    void poll_loop();

    ISerial& serial_;
    WsServer& server_;
    std::atomic<int> poll_interval_ms_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace obd
