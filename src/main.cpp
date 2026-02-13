#include <signal.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#ifdef ARROWBD2_MOCK
#include "mock_serial.h"
#endif
#include "pipeline.h"
#include "serial.h"
#include "ws_server.h"

static obd::WsServer* g_server = nullptr;

static void signal_handler(int) {
    if (g_server) g_server->stop();
}

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "  --device PATH   Serial device (default: /dev/ttyUSB0)\n"
              << "  --baud RATE     Baud rate (default: 38400)\n"
              << "  --port PORT     WebSocket server port (default: 8080)\n"
              << "  --poll-ms MS    Poll interval in ms (default: 200)\n"
              << "  --help          Show this help\n";
}

static constexpr const char* INDEX_HTML = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>ArrowBD2 — Live OBD-2 Dashboard</title>
<link rel="stylesheet" crossorigin="anonymous"
  href="https://cdn.jsdelivr.net/npm/@finos/perspective-viewer/dist/css/themes.css" />
<style>
  html, body { margin: 0; padding: 0; height: 100%; overflow: hidden; background: #1a1a2e; }
  #dashboard { display: grid; grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr; height: 100%; width: 100%; }
  #speed { grid-column: 1; grid-row: 1; }
  #throttle { grid-column: 2; grid-row: 1; }
  #rpm { grid-column: 1 / -1; grid-row: 2; }
  perspective-viewer { width: 100%; height: 100%; }
  #status { position: fixed; top: 8px; right: 12px; color: #888; font: 12px monospace; z-index: 999; }
</style>
</head>
<body>
<div id="status">Loading Perspective...</div>
<div id="dashboard">
  <perspective-viewer id="speed" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="throttle" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="rpm" theme="Pro Dark"></perspective-viewer>
</div>
<script type="module">
  const status = document.getElementById("status");

  // Import Perspective components
  const [perspectiveMod] = await Promise.all([
    import("https://cdn.jsdelivr.net/npm/@finos/perspective/dist/cdn/perspective.js"),
    import("https://cdn.jsdelivr.net/npm/@finos/perspective-viewer/dist/cdn/perspective-viewer.js"),
    import("https://cdn.jsdelivr.net/npm/@finos/perspective-viewer-datagrid/dist/cdn/perspective-viewer-datagrid.js"),
    import("https://cdn.jsdelivr.net/npm/@finos/perspective-viewer-d3fc/dist/cdn/perspective-viewer-d3fc.js"),
  ]);
  const perspective = perspectiveMod.default;

  status.textContent = "Perspective loaded. Connecting WebSocket...";

  const viewers = [
    { el: document.getElementById("speed"), columns: ["speed_kmh"] },
    { el: document.getElementById("throttle"), columns: ["throttle_pct"] },
    { el: document.getElementById("rpm"), columns: ["rpm"] },
  ];
  const worker = await perspective.worker();
  let table = null;
  let msgCount = 0;

  const ws = new WebSocket("ws://" + location.host, "obd");
  ws.binaryType = "arraybuffer";

  ws.onopen = () => { status.textContent = "Connected"; };
  ws.onclose = () => { status.textContent = "Disconnected"; };
  ws.onerror = () => { status.textContent = "WebSocket error"; };

  ws.onmessage = async (event) => {
    msgCount++;
    const arrow = new Uint8Array(event.data);
    status.textContent = "Messages: " + msgCount + " (" + arrow.length + " bytes)";
    try {
      if (!table) {
        table = await worker.table(arrow);
        for (const v of viewers) {
          await v.el.load(table);
          await v.el.restore({
            plugin: "Y Line",
            columns: v.columns,
            sort: [["timestamp_ms", "asc"]],
          });
        }
      } else {
        table.update(arrow);
      }
    } catch (e) {
      status.textContent = "Error: " + e.message;
      console.error(e);
    }
  };
</script>
</body>
</html>
)html";

int main(int argc, char* argv[]) {
    std::string device = "/dev/ttyUSB0";
    int baud = 38400;
    int port = 8080;
    int poll_ms = 200;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            device = argv[++i];
        } else if (std::strcmp(argv[i], "--baud") == 0 && i + 1 < argc) {
            baud = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--poll-ms") == 0 && i + 1 < argc) {
            poll_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Create serial port
    std::unique_ptr<obd::ISerial> serial;
#ifdef ARROWBD2_MOCK
    std::cerr << "Running in MOCK mode (simulated OBD data)\n";
    serial = std::make_unique<obd::MockSerial>();
#else
    serial = std::make_unique<obd::PosixSerial>();
#endif

    if (!serial->open(device, baud)) {
#ifndef ARROWBD2_MOCK
        std::cerr << "Failed to open serial device: " << device << "\n";
        return 1;
#endif
    }

    // WebSocket server
    obd::WsServer::Options ws_opts;
    ws_opts.port = port;
    obd::WsServer server(ws_opts);
    server.set_index_html(INDEX_HTML);

    // Install signal handler (needs server pointer)
    g_server = &server;
    struct sigaction sa {};
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // Pipeline
    obd::PipelineOptions pipe_opts;
    pipe_opts.poll_interval_ms = poll_ms;
    obd::Pipeline pipeline(*serial, server, pipe_opts);

    std::cerr << "Starting arrowbd2 on port " << port << "\n";
    std::cerr << "Open http://localhost:" << port << " in your browser\n";

    // Start OBD polling thread
    pipeline.start();

    // Run WebSocket event loop on main thread (blocks until stop)
    server.run();

    std::cerr << "Shutting down...\n";
    pipeline.stop();
    serial->close();
    g_server = nullptr;
    return 0;
}
