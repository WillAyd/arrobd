#include "../src/elm327.h"
#include "../src/pids.h"

#include <cassert>
#include <cmath>
#include <iostream>

void test_parse_hex_bytes() {
    auto result = obd::parse_hex_bytes("41 0C 1A F8");
    assert(result.has_value());
    assert(result->size() == 4);
    assert((*result)[0] == 0x41);
    assert((*result)[1] == 0x0C);
    assert((*result)[2] == 0x1A);
    assert((*result)[3] == 0xF8);
    std::cerr << "  parse_hex_bytes: OK\n";
}

void test_parse_response_rpm() {
    auto result = obd::parse_response("41 0C 1A F8\r\r>");
    assert(result.has_value());
    assert(result->mode == 0x41);
    assert(result->pid == 0x0C);
    assert(result->data.size() == 2);
    assert(result->data[0] == 0x1A);
    assert(result->data[1] == 0xF8);

    // Verify RPM calculation: (256*0x1A + 0xF8) / 4 = (256*26 + 248) / 4 = 6904/4 = 1726
    const auto& pids = obd::pid_table();
    const auto& rpm_pid = pids[0];  // rpm is first
    double rpm = rpm_pid.parse(result->data);
    assert(std::abs(rpm - 1726.0) < 0.01);
    std::cerr << "  parse_response rpm=" << rpm << ": OK\n";
}

void test_parse_response_speed() {
    auto result = obd::parse_response("41 0D 3C\r\r>");
    assert(result.has_value());
    assert(result->mode == 0x41);
    assert(result->pid == 0x0D);
    assert(result->data.size() == 1);
    assert(result->data[0] == 0x3C);

    const auto& pids = obd::pid_table();
    const auto& speed_pid = pids[1];  // speed_kmh is second
    double speed = speed_pid.parse(result->data);
    assert(std::abs(speed - 60.0) < 0.01);
    std::cerr << "  parse_response speed=" << speed << ": OK\n";
}

void test_parse_response_coolant() {
    auto result = obd::parse_response("41 05 7B\r\r>");
    assert(result.has_value());

    const auto& pids = obd::pid_table();
    const auto& coolant_pid = pids[2];  // coolant_temp_c is third
    double temp = coolant_pid.parse(result->data);
    // 0x7B = 123, temp = 123 - 40 = 83
    assert(std::abs(temp - 83.0) < 0.01);
    std::cerr << "  parse_response coolant=" << temp << ": OK\n";
}

void test_parse_no_data() {
    auto result = obd::parse_response("NO DATA\r\r>");
    assert(!result.has_value());
    assert(result.error() == obd::ElmError::NoData);
    std::cerr << "  parse NO DATA: OK\n";
}

void test_parse_error() {
    auto result = obd::parse_response("?\r\r>");
    assert(!result.has_value());
    assert(result.error() == obd::ElmError::InvalidResponse);
    std::cerr << "  parse error '?': OK\n";
}

void test_pid_command() {
    assert(obd::pid_command(0x010C) == "010C");
    assert(obd::pid_command(0x010D) == "010D");
    assert(obd::pid_command(0x0105) == "0105");
    std::cerr << "  pid_command: OK\n";
}

int main() {
    std::cerr << "Running ELM327 tests...\n";
    test_parse_hex_bytes();
    test_parse_response_rpm();
    test_parse_response_speed();
    test_parse_response_coolant();
    test_parse_no_data();
    test_parse_error();
    test_pid_command();
    std::cerr << "All ELM327 tests passed.\n";
    return 0;
}
