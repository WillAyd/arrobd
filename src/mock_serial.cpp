#include "mock_serial.h"

#include <chrono>
#include <format>
#include <thread>

namespace obd {

MockSerial::MockSerial()
    : rng_(static_cast<uint32_t>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {}

bool MockSerial::open(const std::string& /*device*/, int /*baud*/) {
    open_ = true;
    return true;
}

void MockSerial::close() { open_ = false; }

bool MockSerial::write(std::string_view data) {
    if (!open_) return false;
    // Strip trailing \r
    pending_command_ = std::string(data);
    while (!pending_command_.empty() && (pending_command_.back() == '\r' || pending_command_.back() == '\n'))
        pending_command_.pop_back();
    return true;
}

std::optional<std::string> MockSerial::read_until_prompt(int /*timeout_ms*/) {
    if (!open_) return std::nullopt;

    // Small delay to simulate serial latency
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    return generate_response(pending_command_);
}

bool MockSerial::is_open() const { return open_; }

double MockSerial::random_walk(double current, double mean, double step, double min_val, double max_val) {
    std::normal_distribution<double> dist(0.0, step);
    double reversion = (mean - current) * 0.05;  // mean reversion
    double next = current + dist(rng_) + reversion;
    return std::clamp(next, min_val, max_val);
}

std::string MockSerial::format_elm_response(uint8_t mode, uint8_t pid, const std::vector<uint8_t>& data) {
    std::string resp = std::format("{:02X} {:02X}", mode, pid);
    for (auto b : data) resp += std::format(" {:02X}", b);
    resp += "\r\r>";
    return resp;
}

std::string MockSerial::generate_response(std::string_view command) {
    // AT commands
    if (command.starts_with("AT") || command.starts_with("at")) {
        if (command == "ATZ") return "ELM327 v1.5\r\r>";
        return "OK\r\r>";
    }

    // OBD PID queries
    if (command == "010C") {
        rpm_ = random_walk(rpm_, 2000.0, 100.0, 600.0, 6000.0);
        uint16_t raw = static_cast<uint16_t>(rpm_ * 4.0);
        return format_elm_response(0x41, 0x0C,
                                   {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw & 0xFF)});
    }
    if (command == "010D") {
        speed_ = random_walk(speed_, 60.0, 5.0, 0.0, 200.0);
        return format_elm_response(0x41, 0x0D, {static_cast<uint8_t>(std::clamp(speed_, 0.0, 255.0))});
    }
    if (command == "0105") {
        coolant_ = random_walk(coolant_, 90.0, 1.0, 40.0, 120.0);
        return format_elm_response(0x41, 0x05, {static_cast<uint8_t>(coolant_ + 40.0)});
    }
    if (command == "0104") {
        load_ = random_walk(load_, 30.0, 3.0, 0.0, 100.0);
        return format_elm_response(0x41, 0x04, {static_cast<uint8_t>(load_ * 255.0 / 100.0)});
    }
    if (command == "0111") {
        throttle_ = random_walk(throttle_, 20.0, 3.0, 0.0, 100.0);
        return format_elm_response(0x41, 0x11, {static_cast<uint8_t>(throttle_ * 255.0 / 100.0)});
    }
    if (command == "010F") {
        intake_temp_ = random_walk(intake_temp_, 30.0, 1.0, -20.0, 60.0);
        return format_elm_response(0x41, 0x0F, {static_cast<uint8_t>(intake_temp_ + 40.0)});
    }
    if (command == "0110") {
        maf_ = random_walk(maf_, 5.0, 0.5, 0.0, 50.0);
        uint16_t raw = static_cast<uint16_t>(maf_ * 100.0);
        return format_elm_response(0x41, 0x10,
                                   {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw & 0xFF)});
    }
    if (command == "010E") {
        timing_advance_ = random_walk(timing_advance_, 12.0, 2.0, -64.0, 63.5);
        uint8_t raw = static_cast<uint8_t>((timing_advance_ + 64.0) * 2.0);
        return format_elm_response(0x41, 0x0E, {raw});
    }
    if (command == "010A") {
        fuel_pressure_ = random_walk(fuel_pressure_, 250.0, 10.0, 0.0, 765.0);
        uint8_t raw = static_cast<uint8_t>(fuel_pressure_ / 3.0);
        return format_elm_response(0x41, 0x0A, {raw});
    }
    if (command == "010B") {
        intake_manifold_ = random_walk(intake_manifold_, 35.0, 3.0, 0.0, 255.0);
        return format_elm_response(0x41, 0x0B, {static_cast<uint8_t>(intake_manifold_)});
    }
    if (command == "0106") {
        short_fuel_trim_ = random_walk(short_fuel_trim_, 0.0, 2.0, -100.0, 99.2);
        uint8_t raw = static_cast<uint8_t>((short_fuel_trim_ + 100.0) * 1.28);
        return format_elm_response(0x41, 0x06, {raw});
    }
    if (command == "0107") {
        long_fuel_trim_ = random_walk(long_fuel_trim_, 0.0, 1.0, -100.0, 99.2);
        uint8_t raw = static_cast<uint8_t>((long_fuel_trim_ + 100.0) * 1.28);
        return format_elm_response(0x41, 0x07, {raw});
    }
    if (command == "011F") {
        runtime_ += 1.0;
        uint16_t raw = static_cast<uint16_t>(std::clamp(runtime_, 0.0, 65535.0));
        return format_elm_response(0x41, 0x1F,
                                   {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw & 0xFF)});
    }
    if (command == "0121") {
        distance_with_mil_ = random_walk(distance_with_mil_, 0.0, 0.1, 0.0, 65535.0);
        uint16_t raw = static_cast<uint16_t>(distance_with_mil_);
        return format_elm_response(0x41, 0x21,
                                   {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw & 0xFF)});
    }
    if (command == "012F") {
        fuel_level_ = random_walk(fuel_level_, 65.0, 0.5, 0.0, 100.0);
        return format_elm_response(0x41, 0x2F, {static_cast<uint8_t>(fuel_level_ * 255.0 / 100.0)});
    }
    if (command == "0131") {
        distance_since_cleared_ = random_walk(distance_since_cleared_, 5000.0, 1.0, 0.0, 65535.0);
        uint16_t raw = static_cast<uint16_t>(distance_since_cleared_);
        return format_elm_response(0x41, 0x31,
                                   {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw & 0xFF)});
    }
    if (command == "0133") {
        barometric_pressure_ = random_walk(barometric_pressure_, 101.0, 0.5, 70.0, 110.0);
        return format_elm_response(0x41, 0x33, {static_cast<uint8_t>(barometric_pressure_)});
    }
    if (command == "0142") {
        control_module_voltage_ = random_walk(control_module_voltage_, 14.0, 0.2, 10.0, 16.0);
        uint16_t raw = static_cast<uint16_t>(control_module_voltage_ * 1000.0);
        return format_elm_response(0x41, 0x42,
                                   {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw & 0xFF)});
    }
    if (command == "0146") {
        ambient_air_temp_ = random_walk(ambient_air_temp_, 22.0, 0.5, -40.0, 60.0);
        return format_elm_response(0x41, 0x46, {static_cast<uint8_t>(ambient_air_temp_ + 40.0)});
    }

    return "NO DATA\r\r>";
}

}  // namespace obd
