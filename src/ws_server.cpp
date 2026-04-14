#include "ws_server.h"

#include <algorithm>
#include <cstring>

namespace obd {

static WsServer* get_server(struct lws* wsi) {
    return static_cast<WsServer*>(lws_context_user(lws_get_context(wsi)));
}

struct HttpPss {
    bool body_pending;
};

int WsServer::callback_http(struct lws* wsi, enum lws_callback_reasons reason,
                             void* user, void* in, size_t len) {
    auto* pss = static_cast<HttpPss*>(user);

    switch (reason) {
        case LWS_CALLBACK_HTTP: {
            auto* server = get_server(wsi);
            if (!server || server->index_html_.empty()) {
                lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, "Not found");
                return -1;
            }

            // Send HTTP headers
            uint8_t buf[LWS_PRE + 512];
            auto* p = buf + LWS_PRE;
            auto* end = p + sizeof(buf) - LWS_PRE;

            if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                                            "text/html; charset=utf-8",
                                            static_cast<lws_filepos_t>(server->index_html_.size()),
                                            &p, end) != 0)
                return 1;
            if (lws_finalize_write_http_header(wsi, buf + LWS_PRE, &p, end) != 0) return 1;

            // Request writable callback to send body
            if (pss) pss->body_pending = true;
            lws_callback_on_writable(wsi);
            return 0;
        }
        case LWS_CALLBACK_HTTP_WRITEABLE: {
            if (!pss || !pss->body_pending) return 0;

            auto* server = get_server(wsi);
            if (!server) return -1;

            // Write body with LWS_PRE padding
            std::vector<uint8_t> body_buf(LWS_PRE + server->index_html_.size());
            std::memcpy(body_buf.data() + LWS_PRE, server->index_html_.data(),
                       server->index_html_.size());
            lws_write(wsi, body_buf.data() + LWS_PRE, server->index_html_.size(),
                     LWS_WRITE_HTTP_FINAL);

            pss->body_pending = false;
            if (lws_http_transaction_completed(wsi)) return -1;
            return 0;
        }
        default:
            break;
    }
    return lws_callback_http_dummy(wsi, reason, nullptr, in, len);
}

int WsServer::callback_ws(struct lws* wsi, enum lws_callback_reasons reason,
                           void* user, void* in, size_t len) {
    auto* pss = static_cast<PerSessionData*>(user);

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            auto* server = get_server(wsi);
            if (server) {
                std::lock_guard lock(server->clients_mutex_);
                server->clients_.push_back(wsi);
            }
            break;
        }
        case LWS_CALLBACK_CLOSED: {
            auto* server = get_server(wsi);
            if (server) {
                std::lock_guard lock(server->clients_mutex_);
                auto& c = server->clients_;
                c.erase(std::remove(c.begin(), c.end(), wsi), c.end());
            }
            break;
        }
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            auto* server = get_server(wsi);
            if (!server) break;

            std::lock_guard lock(server->data_mutex_);
            if (server->has_pending_ && server->pending_payload_len_ > 0) {
                lws_write(wsi, server->pending_data_.data() + LWS_PRE,
                         server->pending_payload_len_, LWS_WRITE_BINARY);
            }
            if (pss) pss->has_pending = false;
            break;
        }
        case LWS_CALLBACK_RECEIVE: {
            auto* server = get_server(wsi);
            if (server && server->on_command_ && in && len > 0) {
                std::string msg(static_cast<const char*>(in), len);
                server->on_command_(msg);
            }
            break;
        }
        case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
            auto* server = get_server(wsi);
            if (server) {
                std::lock_guard lock(server->clients_mutex_);
                for (auto* client : server->clients_) {
                    lws_callback_on_writable(client);
                }
            }
            break;
        }
        default:
            break;
    }
    return 0;
}

static const struct lws_protocols protocols[] = {
    {"http", WsServer::callback_http, sizeof(HttpPss), 0, 0, nullptr, 0},
    {"obd", WsServer::callback_ws, sizeof(WsServer::PerSessionData), 4096, 0, nullptr, 0},
    LWS_PROTOCOL_LIST_TERM,
};

static const struct lws_http_mount http_mount = {
    .mount_next = nullptr,
    .mountpoint = "/",
    .origin = nullptr,
    .def = nullptr,
    .protocol = "http",
    .cgienv = nullptr,
    .extra_mimetypes = nullptr,
    .interpret = nullptr,
    .cgi_timeout = 0,
    .cache_max_age = 0,
    .auth_mask = 0,
    .cache_reusable = 0,
    .cache_revalidate = 0,
    .cache_intermediaries = 0,
    .origin_protocol = LWSMPRO_CALLBACK,
    .mountpoint_len = 1,
    .basic_auth_login_file = nullptr,
};

WsServer::WsServer(const Options& opts) : opts_(opts) {
    struct lws_context_creation_info info {};
    info.port = opts_.port;
    info.protocols = protocols;
    info.mounts = &http_mount;
    info.user = this;

    // Suppress most lws logging
    lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);

    context_ = lws_create_context(&info);
}

WsServer::~WsServer() {
    stop();
    if (context_) {
        lws_context_destroy(context_);
        context_ = nullptr;
    }
}

void WsServer::run() {
    if (!context_) return;
    running_ = true;
    while (running_) {
        lws_service(context_, 50);
    }
}

void WsServer::stop() { running_ = false; }

void WsServer::broadcast_binary(const uint8_t* data, size_t len) {
    {
        std::lock_guard lock(data_mutex_);
        pending_data_.resize(LWS_PRE + len);
        std::memcpy(pending_data_.data() + LWS_PRE, data, len);
        pending_payload_len_ = len;
        has_pending_ = true;
    }
    if (context_) {
        lws_cancel_service(context_);
    }
}

void WsServer::set_index_html(std::string html) { index_html_ = std::move(html); }

void WsServer::set_on_command(std::function<void(const std::string&)> cb) {
    on_command_ = std::move(cb);
}

}  // namespace obd
