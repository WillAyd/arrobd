#include "elm327.h"

#include <algorithm>
#include <charconv>

#include "serial.h"

namespace obd {

std::expected<std::vector<uint8_t>, ElmError> parse_hex_bytes(std::string_view line) {
    std::vector<uint8_t> bytes;
    size_t i = 0;
    while (i < line.size()) {
        // Skip whitespace
        while (i < line.size() && (line[i] == ' ' || line[i] == '\r' || line[i] == '\n'))
            ++i;
        if (i >= line.size()) break;

        // Need at least 2 hex chars
        if (i + 1 >= line.size()) return std::unexpected(ElmError::InvalidResponse);

        uint8_t val = 0;
        auto [ptr, ec] = std::from_chars(line.data() + i, line.data() + i + 2, val, 16);
        if (ec != std::errc()) return std::unexpected(ElmError::InvalidResponse);

        bytes.push_back(val);
        i += 2;
    }
    return bytes;
}

std::expected<ElmResponse, ElmError> parse_response(std::string_view raw) {
    // Strip trailing prompt '>' and whitespace
    auto end = raw.find('>');
    if (end != std::string_view::npos) raw = raw.substr(0, end);

    // Trim whitespace
    while (!raw.empty() && (raw.back() == '\r' || raw.back() == '\n' || raw.back() == ' '))
        raw.remove_suffix(1);
    while (!raw.empty() && (raw.front() == '\r' || raw.front() == '\n' || raw.front() == ' '))
        raw.remove_prefix(1);

    if (raw.empty()) return std::unexpected(ElmError::NoData);

    // Check for error responses
    if (raw.find("NO DATA") != std::string_view::npos) return std::unexpected(ElmError::NoData);
    if (raw.find("?") != std::string_view::npos) return std::unexpected(ElmError::InvalidResponse);
    if (raw.find("UNABLE TO CONNECT") != std::string_view::npos)
        return std::unexpected(ElmError::NoData);
    if (raw.find("ERROR") != std::string_view::npos)
        return std::unexpected(ElmError::InvalidResponse);

    // Find the last line that starts with hex (the actual response, skip echo)
    std::string_view data_line = raw;
    if (auto pos = raw.rfind('\r'); pos != std::string_view::npos) {
        auto candidate = raw.substr(pos + 1);
        while (!candidate.empty() && (candidate.front() == '\r' || candidate.front() == '\n' || candidate.front() == ' '))
            candidate.remove_prefix(1);
        if (!candidate.empty()) data_line = candidate;
    }

    auto bytes_result = parse_hex_bytes(data_line);
    if (!bytes_result) return std::unexpected(bytes_result.error());

    auto& bytes = *bytes_result;
    if (bytes.size() < 2) return std::unexpected(ElmError::InvalidResponse);

    ElmResponse resp;
    resp.mode = bytes[0];
    resp.pid = bytes[1];
    resp.data.assign(bytes.begin() + 2, bytes.end());
    return resp;
}

Elm327::Elm327(ISerial& serial) : serial_(serial) {}

bool Elm327::init() {
    // Reset
    serial_.write("ATZ\r");
    serial_.read_until_prompt(2000);

    // Echo off
    serial_.write("ATE0\r");
    serial_.read_until_prompt(1000);

    // Linefeeds off
    serial_.write("ATL0\r");
    serial_.read_until_prompt(1000);

    // Spaces on (easier to parse)
    serial_.write("ATS1\r");
    serial_.read_until_prompt(1000);

    // Auto protocol
    serial_.write("ATSP0\r");
    serial_.read_until_prompt(1000);

    return true;
}

std::expected<ElmResponse, ElmError> Elm327::query_raw(std::string_view command) {
    std::string cmd(command);
    cmd += '\r';
    if (!serial_.write(cmd)) return std::unexpected(ElmError::SerialError);

    auto response = serial_.read_until_prompt(2000);
    if (!response) return std::unexpected(ElmError::Timeout);

    return parse_response(*response);
}

}  // namespace obd
