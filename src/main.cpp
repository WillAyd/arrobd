#include <signal.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#ifdef ARROBD_MOCK
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
<title>arrobd — Live OBD-II Dashboard</title>
<link rel="stylesheet" crossorigin="anonymous"
  href="https://cdn.jsdelivr.net/npm/@finos/perspective-viewer/dist/css/themes.css" />
<style>
  html, body { margin: 0; padding: 0; height: 100%; overflow: hidden; background: #1a1a2e; color: #ccc; font-family: sans-serif; }
  #header { display: flex; align-items: center; justify-content: space-between; padding: 0 12px; height: 40px; background: #12122a; border-bottom: 1px solid #333; }
  #header .logo { height: 28px; margin-right: 8px; }
  #status { color: #888; font: 12px monospace; }
  .tab-bar { display: flex; gap: 4px; }
  .tab-bar button {
    background: transparent; border: none; color: #888; font: 14px sans-serif;
    padding: 8px 16px; cursor: pointer; border-bottom: 2px solid transparent; transition: color 0.2s, border-color 0.2s;
  }
  .tab-bar button:hover { color: #bbb; }
  .tab-bar button.active { color: #fff; border-bottom-color: #5b8def; }
  #controls { display: flex; align-items: center; gap: 12px; padding: 0 12px; height: 36px; background: #0e0e22; border-bottom: 1px solid #282840; }
  #controls button, #controls select, #controls input {
    background: #1e1e3a; border: 1px solid #3a3a5c; color: #ccc; font: 12px sans-serif;
    padding: 4px 12px; border-radius: 4px; cursor: pointer;
  }
  #controls button:hover, #controls select:hover, #controls input:hover { border-color: #5b8def; color: #fff; }
  #controls label { color: #888; font: 12px sans-serif; }
  #btn-pause.paused { background: #2a4a2a; border-color: #4a8f4a; }
  .tab-panel { display: none; height: calc(100% - 76px); }
  .tab-panel.active { display: grid; }
  #tab-performance { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr 1fr; }
  #tab-performance .full-width { grid-column: 1 / -1; }
  #tab-engine { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr; }
  #tab-engine .full-width { grid-column: 1 / -1; }
  #tab-fuel { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr 1fr; }
  #tab-diagnostics { grid-template-columns: 1fr 1fr; grid-template-rows: 1fr 1fr; }
  .chart-cell { display: flex; flex-direction: column; overflow: hidden; }
  .chart-title { color: #aaa; font: 600 13px sans-serif; padding: 6px 8px 2px; background: #12122a; white-space: nowrap; text-align: center; }
  perspective-viewer { width: 100%; flex: 1; min-height: 0; --d3fc-label--color: transparent; }
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
<div id="controls">
  <button id="btn-pause" onclick="togglePause()">Pause</button>
  <label>Poll rate:
    <select id="sel-poll" onchange="changePollRate(this.value)">
      <option value="100">100ms</option>
      <option value="200" selected>200ms</option>
      <option value="500">500ms</option>
      <option value="1000">1s</option>
      <option value="2000">2s</option>
    </select>
  </label>
  <button id="btn-reset" onclick="resetCharts()">Reset</button>
</div>

<div id="tab-performance" class="tab-panel active">
  <div class="chart-cell"><div class="chart-title">Speed (km/h)</div><perspective-viewer id="v-speed" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">RPM</div><perspective-viewer id="v-rpm" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">Throttle (%)</div><perspective-viewer id="v-throttle" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">Engine Load (%)</div><perspective-viewer id="v-load" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell full-width"><div class="chart-title">Timing Advance (&deg;)</div><perspective-viewer id="v-timing" theme="Pro Dark"></perspective-viewer></div>
</div>

<div id="tab-engine" class="tab-panel">
  <div class="chart-cell"><div class="chart-title">Coolant Temp (&deg;C)</div><perspective-viewer id="v-coolant" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">Intake Air Temp (&deg;C)</div><perspective-viewer id="v-intake-temp" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell full-width"><div class="chart-title">Control Module Voltage (V)</div><perspective-viewer id="v-voltage" theme="Pro Dark"></perspective-viewer></div>
</div>

<div id="tab-fuel" class="tab-panel">
  <div class="chart-cell"><div class="chart-title">MAF (g/s)</div><perspective-viewer id="v-maf" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">Fuel Pressure (kPa)</div><perspective-viewer id="v-fuel-pressure" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">Intake Manifold (kPa)</div><perspective-viewer id="v-intake-manifold" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">Short Fuel Trim (%)</div><perspective-viewer id="v-short-trim" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">Long Fuel Trim (%)</div><perspective-viewer id="v-long-trim" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">Fuel Level (%)</div><perspective-viewer id="v-fuel-level" theme="Pro Dark"></perspective-viewer></div>
</div>

<div id="tab-diagnostics" class="tab-panel">
  <div class="chart-cell"><div class="chart-title">Ambient Air Temp (&deg;C)</div><perspective-viewer id="v-ambient-temp" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">Barometric Pressure (kPa)</div><perspective-viewer id="v-barometric" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">Distance w/ MIL (km)</div><perspective-viewer id="v-dist-mil" theme="Pro Dark"></perspective-viewer></div>
  <div class="chart-cell"><div class="chart-title">Distance Since Cleared (km)</div><perspective-viewer id="v-dist-cleared" theme="Pro Dark"></perspective-viewer></div>
</div>

<script>
  var paused = false;

  function switchTab(name) {
    document.querySelectorAll(".tab-panel").forEach(p => p.classList.remove("active"));
    document.querySelectorAll(".tab-bar button").forEach(b => b.classList.remove("active"));
    const panel = document.getElementById("tab-" + name);
    panel.classList.add("active");
    event.currentTarget.classList.add("active");
    panel.querySelectorAll("perspective-viewer").forEach(v => v.notifyResize());
  }

  function togglePause() {
    paused = !paused;
    const btn = document.getElementById("btn-pause");
    btn.textContent = paused ? "Resume" : "Pause";
    btn.classList.toggle("paused", paused);
  }

  function changePollRate(val) {
    if (window._obdWs && window._obdWs.readyState === WebSocket.OPEN) {
      window._obdWs.send(JSON.stringify({ poll_interval_ms: parseInt(val) }));
      window._needsTableRecreate = true;
    }
  }

  function resetCharts() {
    window._needsTableRecreate = true;
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

  window._needsTableRecreate = false;

  async function recreateTable(arrow) {
    const oldTable = table;
    table = await worker.table(arrow);
    for (const v of viewers) {
      await v.el.load(table);
      await v.el.restore({
        plugin: "Y Line",
        columns: v.columns,
        sort: [["timestamp_ms", "asc"]],
      });
    }
    if (oldTable) await oldTable.delete();
  }

  const ws = new WebSocket("ws://" + location.host, "obd");
  ws.binaryType = "arraybuffer";
  window._obdWs = ws;

  ws.onopen = () => { status.textContent = "Connected"; };
  ws.onclose = () => { status.textContent = "Disconnected"; };
  ws.onerror = () => { status.textContent = "WebSocket error"; };

  ws.onmessage = async (event) => {
    msgCount++;
    const arrow = new Uint8Array(event.data);
    status.textContent = "Messages: " + msgCount + " (" + arrow.length + " bytes)";
    if (paused) return;
    try {
      if (!table) {
        await recreateTable(arrow);
      } else if (window._needsTableRecreate) {
        window._needsTableRecreate = false;
        const configs = await Promise.all(viewers.map(v => v.el.save()));
        const oldTable = table;
        table = await worker.table(arrow);
        for (let i = 0; i < viewers.length; i++) {
          await viewers[i].el.load(table);
          await viewers[i].el.restore(configs[i]);
        }
        if (oldTable) await oldTable.delete();
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
#ifdef ARROBD_MOCK
    std::cerr << "Running in MOCK mode (simulated OBD data)\n";
    serial = std::make_unique<obd::MockSerial>();
#else
    serial = std::make_unique<obd::PosixSerial>();
#endif

    if (!serial->open(device, baud)) {
#ifndef ARROBD_MOCK
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

    server.set_on_command([&pipeline](const std::string& msg) {
        // Parse {"poll_interval_ms": N}
        auto pos = msg.find("\"poll_interval_ms\"");
        if (pos == std::string::npos) return;
        pos = msg.find(':', pos);
        if (pos == std::string::npos) return;
        pos++;
        while (pos < msg.size() && (msg[pos] == ' ' || msg[pos] == '\t')) pos++;
        int val = std::atoi(msg.c_str() + pos);
        if (val >= 50 && val <= 5000) {
            pipeline.set_poll_interval(val);
            std::cerr << "Poll interval changed to " << val << "ms\n";
        }
    });

    std::cerr << "Starting arrobd on port " << port << "\n";
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
