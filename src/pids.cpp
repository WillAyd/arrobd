#include "pids.h"

#include <format>

namespace obd {

const std::vector<PidDef>& pid_table() {
    static const std::vector<PidDef> table = {
        {0x010C, "rpm", 2,
         [](std::span<const uint8_t> d) -> double {
             return (256.0 * d[0] + d[1]) / 4.0;
         }},
        {0x010D, "speed_kmh", 1,
         [](std::span<const uint8_t> d) -> double {
             return static_cast<double>(d[0]);
         }},
        {0x0105, "coolant_temp_c", 1,
         [](std::span<const uint8_t> d) -> double {
             return static_cast<double>(d[0]) - 40.0;
         }},
        {0x0104, "engine_load_pct", 1,
         [](std::span<const uint8_t> d) -> double {
             return d[0] * 100.0 / 255.0;
         }},
        {0x0111, "throttle_pct", 1,
         [](std::span<const uint8_t> d) -> double {
             return d[0] * 100.0 / 255.0;
         }},
        {0x010F, "intake_air_temp_c", 1,
         [](std::span<const uint8_t> d) -> double {
             return static_cast<double>(d[0]) - 40.0;
         }},
        {0x0110, "maf_gps", 2,
         [](std::span<const uint8_t> d) -> double {
             return (256.0 * d[0] + d[1]) / 100.0;
         }},
    };
    return table;
}

std::string pid_command(uint16_t pid) {
    return std::format("{:04X}", pid);
}

}  // namespace obd
