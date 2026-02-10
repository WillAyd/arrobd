#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace obd {

struct PidDef {
    uint16_t pid;            // e.g. 0x010C
    std::string name;        // e.g. "rpm"
    uint8_t response_bytes;  // number of data bytes expected (1 or 2)
    std::function<double(std::span<const uint8_t>)> parse;
};

const std::vector<PidDef>& pid_table();

// Build the AT command string for a PID, e.g. "010C"
std::string pid_command(uint16_t pid);

}  // namespace obd
