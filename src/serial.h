#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace obd {

class ISerial {
public:
    virtual ~ISerial() = default;
    virtual bool open(const std::string& device, int baud = 38400) = 0;
    virtual void close() = 0;
    virtual bool write(std::string_view data) = 0;
    // Read until '>' prompt or timeout (ms). Returns response or nullopt on timeout.
    virtual std::optional<std::string> read_until_prompt(int timeout_ms = 2000) = 0;
    virtual bool is_open() const = 0;
};

class PosixSerial : public ISerial {
public:
    PosixSerial() = default;
    ~PosixSerial() override;

    bool open(const std::string& device, int baud = 38400) override;
    void close() override;
    bool write(std::string_view data) override;
    std::optional<std::string> read_until_prompt(int timeout_ms = 2000) override;
    bool is_open() const override;

private:
    int fd_ = -1;
};

}  // namespace obd
