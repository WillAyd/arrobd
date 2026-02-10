#pragma once

#include <libwebsockets.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace obd {

class WsServer {
public:
    struct Options {
        int port = 8080;
        std::string frontend_dir = "frontend";
    };

    explicit WsServer(const Options& opts);
    ~WsServer();

    // Run the event loop (blocking, call from dedicated thread or main)
    void run();

    // Stop the event loop
    void stop();

    // Thread-safe: queue binary data for broadcast to all WS clients
    void broadcast_binary(const uint8_t* data, size_t len);

    // Set the HTML content to serve on GET /
    void set_index_html(std::string html);

    // Public for C callback registration in protocols array
    static int callback_http(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len);
    static int callback_ws(struct lws* wsi, enum lws_callback_reasons reason,
                           void* user, void* in, size_t len);

    struct PerSessionData {
        bool has_pending = false;
    };

private:
    Options opts_;
    struct lws_context* context_ = nullptr;
    bool running_ = false;

    // Connected WS clients
    std::mutex clients_mutex_;
    std::vector<struct lws*> clients_;

    // Pending broadcast data (LWS_PRE padding included)
    std::mutex data_mutex_;
    std::vector<uint8_t> pending_data_;  // with LWS_PRE prefix space
    size_t pending_payload_len_ = 0;
    bool has_pending_ = false;

    // Index HTML content
    std::string index_html_;
};

}  // namespace obd
