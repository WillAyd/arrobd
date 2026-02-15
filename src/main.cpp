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
  html, body { margin: 0; padding: 0; height: 100%; overflow: hidden; background: #1a1a2e; color: #ccc; font-family: sans-serif; }
  #header { display: flex; align-items: center; justify-content: space-between; padding: 0 12px; height: 40px; background: #12122a; border-bottom: 1px solid #333; }
  #status { color: #888; font: 12px monospace; }
  .tab-bar { display: flex; gap: 4px; }
  .tab-bar button {
    background: transparent; border: none; color: #888; font: 14px sans-serif;
    padding: 8px 16px; cursor: pointer; border-bottom: 2px solid transparent; transition: color 0.2s, border-color 0.2s;
  }
  .tab-bar button:hover { color: #bbb; }
  .tab-bar button.active { color: #fff; border-bottom-color: #5b8def; }
  .tab-panel { display: none; height: calc(100% - 40px); }
  .tab-panel.active { display: grid; }
  #tab-performance { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr 1fr; }
  #tab-performance .full-width { grid-column: 1 / -1; }
  #tab-engine { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr; }
  #tab-fuel { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr 1fr; }
  #tab-diagnostics { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr; }
  perspective-viewer { width: 100%; height: 100%; }
</style>
</head>
<body>
<div id="header">
  <div class="tab-bar">
    <button class="active" onclick="switchTab('performance')">Performance</button>
    <button onclick="switchTab('engine')">Engine Health</button>
    <button onclick="switchTab('fuel')">Fuel &amp; Air</button>
    <button onclick="switchTab('diagnostics')">Diagnostics</button>
  </div>
  <div id="status">Loading Perspective...</div>
</div>

<div id="tab-performance" class="tab-panel active">
  <perspective-viewer id="v-speed" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-rpm" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-throttle" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-load" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-timing" class="full-width" theme="Pro Dark"></perspective-viewer>
</div>

<div id="tab-engine" class="tab-panel">
  <perspective-viewer id="v-coolant" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-intake-temp" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-voltage" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-runtime" theme="Pro Dark"></perspective-viewer>
</div>

<div id="tab-fuel" class="tab-panel">
  <perspective-viewer id="v-maf" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-fuel-pressure" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-intake-manifold" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-short-trim" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-long-trim" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-fuel-level" theme="Pro Dark"></perspective-viewer>
</div>

<div id="tab-diagnostics" class="tab-panel">
  <perspective-viewer id="v-ambient-temp" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-barometric" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-dist-mil" theme="Pro Dark"></perspective-viewer>
  <perspective-viewer id="v-dist-cleared" theme="Pro Dark"></perspective-viewer>
</div>

<script>
  function switchTab(name) {
    document.querySelectorAll(".tab-panel").forEach(p => p.classList.remove("active"));
    document.querySelectorAll(".tab-bar button").forEach(b => b.classList.remove("active"));
    const panel = document.getElementById("tab-" + name);
    panel.classList.add("active");
    event.currentTarget.classList.add("active");
    panel.querySelectorAll("perspective-viewer").forEach(v => v.notifyResize());
  }
</script>
<script type="module">
  const status = document.getElementById("status");

  const [perspectiveMod] = await Promise.all([
    import("https://cdn.jsdelivr.net/npm/@finos/perspective/dist/cdn/perspective.js"),
    import("https://cdn.jsdelivr.net/npm/@finos/perspective-viewer/dist/cdn/perspective-viewer.js"),
    import("https://cdn.jsdelivr.net/npm/@finos/perspective-viewer-datagrid/dist/cdn/perspective-viewer-datagrid.js"),
    import("https://cdn.jsdelivr.net/npm/@finos/perspective-viewer-d3fc/dist/cdn/perspective-viewer-d3fc.js"),
  ]);
  const perspective = perspectiveMod.default;

  status.textContent = "Perspective loaded. Connecting WebSocket...";

  const viewers = [
    { el: document.getElementById("v-speed"), columns: ["speed_kmh"] },
    { el: document.getElementById("v-rpm"), columns: ["rpm"] },
    { el: document.getElementById("v-throttle"), columns: ["throttle_pct"] },
    { el: document.getElementById("v-load"), columns: ["engine_load_pct"] },
    { el: document.getElementById("v-timing"), columns: ["timing_advance_deg"] },
    { el: document.getElementById("v-coolant"), columns: ["coolant_temp_c"] },
    { el: document.getElementById("v-intake-temp"), columns: ["intake_air_temp_c"] },
    { el: document.getElementById("v-voltage"), columns: ["control_module_voltage_v"] },
    { el: document.getElementById("v-runtime"), columns: ["runtime_s"] },
    { el: document.getElementById("v-maf"), columns: ["maf_gps"] },
    { el: document.getElementById("v-fuel-pressure"), columns: ["fuel_pressure_kpa"] },
    { el: document.getElementById("v-intake-manifold"), columns: ["intake_manifold_kpa"] },
    { el: document.getElementById("v-short-trim"), columns: ["short_fuel_trim_pct"] },
    { el: document.getElementById("v-long-trim"), columns: ["long_fuel_trim_pct"] },
    { el: document.getElementById("v-fuel-level"), columns: ["fuel_level_pct"] },
    { el: document.getElementById("v-ambient-temp"), columns: ["ambient_air_temp_c"] },
    { el: document.getElementById("v-barometric"), columns: ["barometric_pressure_kpa"] },
    { el: document.getElementById("v-dist-mil"), columns: ["distance_with_mil_km"] },
    { el: document.getElementById("v-dist-cleared"), columns: ["distance_since_cleared_km"] },
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
