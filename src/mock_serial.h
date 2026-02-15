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
    double timing_advance_ = 10.0;
    double fuel_pressure_ = 250.0;
    double intake_manifold_ = 35.0;
    double short_fuel_trim_ = 0.0;
    double long_fuel_trim_ = 0.0;
    double runtime_ = 0.0;
    double distance_with_mil_ = 0.0;
    double fuel_level_ = 65.0;
    double distance_since_cleared_ = 5000.0;
    double barometric_pressure_ = 101.0;
    double control_module_voltage_ = 14.0;
    double ambient_air_temp_ = 22.0;

    double random_walk(double current, double mean, double step, double min_val, double max_val);
};

}  // namespace obd
