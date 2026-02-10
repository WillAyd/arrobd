#pragma once

#include "serial.h"

#include <random>

namespace obd {

// Mock serial that simulates ELM327 responses with mean-reverting random walk values
class MockSerial : public ISerial {
public:
    MockSerial();

    bool open(const std::string& device, int baud = 38400) override;
    void close() override;
    bool write(std::string_view data) override;
    std::optional<std::string> read_until_prompt(int timeout_ms = 2000) override;
    bool is_open() const override;

private:
    std::string generate_response(std::string_view command);
    std::string format_elm_response(uint8_t mode, uint8_t pid, const std::vector<uint8_t>& data);

    bool open_ = false;
    std::string pending_command_;
    std::mt19937 rng_;

    // Simulated sensor state (mean-reverting random walk)
    double rpm_ = 800.0;
    double speed_ = 0.0;
    double coolant_ = 85.0;
    double load_ = 20.0;
    double throttle_ = 15.0;
    double intake_temp_ = 25.0;
    double maf_ = 3.0;

    double random_walk(double current, double mean, double step, double min_val, double max_val);
};

}  // namespace obd
