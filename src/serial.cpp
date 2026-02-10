#include "serial.h"

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cstring>

namespace obd {

PosixSerial::~PosixSerial() { close(); }

bool PosixSerial::open(const std::string& device, int baud) {
    fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return false;

    // Clear non-blocking after open
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

    struct termios tty {};
    if (tcgetattr(fd_, &tty) != 0) {
        close();
        return false;
    }

    speed_t speed;
    switch (baud) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        default: speed = B38400; break;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // 8N1
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    // No flow control
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    // Raw mode
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;  // 100ms inter-byte timeout

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        close();
        return false;
    }

    tcflush(fd_, TCIOFLUSH);
    return true;
}

void PosixSerial::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool PosixSerial::write(std::string_view data) {
    if (fd_ < 0) return false;
    auto written = ::write(fd_, data.data(), data.size());
    return written == static_cast<ssize_t>(data.size());
}

std::optional<std::string> PosixSerial::read_until_prompt(int timeout_ms) {
    if (fd_ < 0) return std::nullopt;

    std::string result;
    result.reserve(256);

    struct pollfd pfd {};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int remaining_ms = timeout_ms;
    while (remaining_ms > 0) {
        int ret = poll(&pfd, 1, remaining_ms);
        if (ret < 0) return std::nullopt;
        if (ret == 0) break;  // timeout

        char buf[256];
        auto n = ::read(fd_, buf, sizeof(buf));
        if (n <= 0) break;

        result.append(buf, static_cast<size_t>(n));
        if (result.find('>') != std::string::npos) return result;

        // Rough timeout tracking — good enough for serial
        remaining_ms -= 50;
    }

    return result.empty() ? std::nullopt : std::optional(result);
}

bool PosixSerial::is_open() const { return fd_ >= 0; }

}  // namespace obd
