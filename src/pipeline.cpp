#include "pipeline.h"

#include <chrono>
#include <iostream>
#include <optional>

#include "batch_builder.h"
#include "elm327.h"
#include "ipc_serializer.h"
#include "pids.h"
#include "serial.h"
#include "ws_server.h"

namespace obd {

Pipeline::Pipeline(ISerial& serial, WsServer& server, const PipelineOptions& opts)
    : serial_(serial), server_(server), poll_interval_ms_(opts.poll_interval_ms) {}

Pipeline::~Pipeline() { stop(); }

void Pipeline::start() {
    running_ = true;
    thread_ = std::thread(&Pipeline::poll_loop, this);
}

void Pipeline::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void Pipeline::set_poll_interval(int ms) {
    poll_interval_ms_.store(ms);
}

void Pipeline::poll_loop() {
    Elm327 elm(serial_);

    std::cerr << "Initializing ELM327...\n";
    if (!elm.init()) {
        std::cerr << "ELM327 init failed\n";
        return;
    }
    std::cerr << "ELM327 ready\n";

    const auto& pids = pid_table();

    // Build column names from PID table
    std::vector<std::string> col_names;
    col_names.reserve(pids.size());
    for (const auto& p : pids) col_names.push_back(p.name);

    BatchBuilder builder(col_names);

    while (running_) {
        auto now = std::chrono::system_clock::now();
        auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now.time_since_epoch())
                         .count();

        std::vector<std::optional<double>> values;
        values.reserve(pids.size());

        for (const auto& pid_def : pids) {
            auto result = elm.query_raw(pid_command(pid_def.pid));
            if (result) {
                auto& resp = *result;
                if (resp.data.size() >= pid_def.response_bytes) {
                    values.push_back(pid_def.parse(resp.data));
                } else {
                    values.push_back(std::nullopt);
                }
            } else {
                values.push_back(std::nullopt);
            }
        }

        auto status = builder.append(ts_ms, values);
        if (!status.ok()) {
            std::cerr << "Arrow append error: " << status.ToString() << "\n";
            continue;
        }

        // Flush every row (low latency) — could batch more for efficiency
        auto batch_result = builder.flush();
        if (!batch_result.ok()) {
            std::cerr << "Arrow flush error: " << batch_result.status().ToString() << "\n";
            continue;
        }

        auto buf_result = serialize_batch(*batch_result);
        if (!buf_result.ok()) {
            std::cerr << "IPC serialize error: " << buf_result.status().ToString() << "\n";
            continue;
        }

        auto& buffer = *buf_result;
        server_.broadcast_binary(buffer->data(), static_cast<size_t>(buffer->size()));

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_.load()));
    }
}

}  // namespace obd
