#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace obd {

class ISerial;

enum class ElmError {
    NoData,
    InvalidResponse,
    SerialError,
    Timeout,
};

struct ElmResponse {
    uint8_t mode;
    uint8_t pid;
    std::vector<uint8_t> data;
};

// Parse a raw ELM327 response string like "41 0C 1A F8\r\r>"
// Returns parsed response or error.
std::expected<ElmResponse, ElmError> parse_response(std::string_view raw);

// Parse hex bytes from a space-separated string like "41 0C 1A F8"
std::expected<std::vector<uint8_t>, ElmError> parse_hex_bytes(std::string_view line);

class Elm327 {
public:
    explicit Elm327(ISerial& serial);

    // Send AT init sequence (ATZ, ATE0, ATL0, ATS0, ATSP0)
    bool init();

    // Query a single PID, return parsed value or nullopt
    std::optional<double> query_pid(uint16_t pid, uint8_t expected_bytes,
                                     std::span<const uint8_t> (*parse_fn)(std::span<const uint8_t>) = nullptr);

    // Query a PID and return raw response
    std::expected<ElmResponse, ElmError> query_raw(std::string_view command);

private:
    ISerial& serial_;
};

}  // namespace obd
