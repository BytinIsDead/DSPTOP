#include "dsptop/ci.h"
#include "dsptop/hal.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <vector>
#include <cmath>
#include <algorithm>

namespace dsptop {
namespace ci {

static std::string EscapeJson(const std::string& s) {
    std::string out; out.reserve(s.size()+8);
    for(char c: s){
        if(c=='"') out+="\\\"";
        else if(c=='\\') out+="\\\\";
        else if(c=='\n') out+="\\n";
        else out+=c;
    }
    return out;
}

std::string SnapshotToJson(const SystemSnapshot& snap) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"hostname\": \"" << EscapeJson(snap.hostname) << "\",\n";
    auto tt = std::chrono::system_clock::to_time_t(snap.timestamp);
    o << "  \"timestamp\": " << tt << ",\n";
    o << "  \"devices\": [\n";
    for(size_t di=0; di<snap.devices.size(); ++di){
        auto& d = snap.devices[di];
        o << "    {\n";
        o << "      \"name\": \"" << EscapeJson(d.device_name) << "\",\n";
        o << "      \"type\": " << (int)d.type << ",\n";
        o << "      \"total_util_pct\": " << std::fixed<<std::setprecision(2)<<d.total_util_pct << ",\n";
        o << "      \"clock_mhz\": " << d.clock_mhz << ",\n";
        o << "      \"power_watts\": " << d.power.power_watts << ",\n";
        o << "      \"power_limit_watts\": " << d.power.power_limit_watts << ",\n";
        o << "      \"envelope_pct\": " << d.power.envelope_pct << ",\n";
        o << "      \"temp_c\": " << d.power.temp_celsius << ",\n";
        o << "      \"thermal_throttle_pct\": " << d.power.thermal_throttle_pct << ",\n";
        o << "      \"sram_util_pct\": " << d.memory.sram_util_pct << ",\n";
        o << "      \"vmem_util_pct\": " << d.memory.vmem_util_pct << ",\n";
        o << "      \"cores\": [\n";
        for(size_t ci=0; ci<d.cores.size(); ++ci){
            auto& c = d.cores[ci];
            o << "        {\"id\":" << c.core_id << ",\"name\":\""<<EscapeJson(c.core_name)<<"\",\"util\":"<<c.utilization_pct<<",\"macc\":"<<c.macc_util_pct<<",\"tops\":"<<c.tops_current<<"}";
            if(ci+1<d.cores.size()) o << ",";
            o << "\n";
        }
        o << "      ]\n";
        o << "    }";
        if(di+1<snap.devices.size()) o << ",";
        o << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}

std::string SnapshotHistoryToJson(const std::vector<SystemSnapshot>& history) {
    std::ostringstream o;
    o << "{\n  \"samples\": [\n";
    for(size_t i=0;i<history.size();++i){
        o << SnapshotToJson(history[i]);
        if(i+1<history.size()) o << ",";
        o << "\n";
    }
    o << "  ],\n";
    // Summary stats for CI summary table
    if(!history.empty()){
        double max_util=0, avg_util=0, max_throttle=0;
        size_t count=0;
        for(auto& s: history) for(auto& d: s.devices){
            max_util = std::max(max_util, d.total_util_pct);
            avg_util += d.total_util_pct; count++;
            max_throttle = std::max(max_throttle, d.power.thermal_throttle_pct);
        }
        if(count) avg_util/=count;
        o << "  \"summary\": {\"max_util\":"<<max_util<<",\"avg_util\":"<<avg_util<<",\"max_throttle\":"<<max_throttle<<",\"samples\":"<<history.size()<<"}\n";
    }
    o << "}\n";
    return o.str();
}

std::string SnapshotHistoryToHtml(const std::vector<SystemSnapshot>& history) {
    if (history.empty()) {
        return "<html><body><h1>No data</h1></body></html>";
    }
    // Compute time labels (seconds from start)
    double interval_sec = 0.5;
    if (history.size() > 1) {
        // estimate from timestamps
        auto t0 = history.front().timestamp;
        auto t1 = history.back().timestamp;
        double dur = std::chrono::duration<double>(t1 - t0).count();
        if (history.size() > 1 && dur > 0) interval_sec = dur / (history.size()-1);
    }
    // Collect devices from first sample (assume stable)
    // size_t ndev = history.front().devices.size(); // (unused, kept for docs)
    // Summary stats
    double max_util=0, avg_util=0, max_throttle=0, max_power=0, avg_power=0;
    size_t count=0;
    for(auto& s: history) for(auto& d: s.devices){
        max_util = std::max(max_util, d.total_util_pct);
        avg_util += d.total_util_pct; count++;
        max_throttle = std::max(max_throttle, d.power.thermal_throttle_pct);
        max_power = std::max(max_power, d.power.power_watts);
        avg_power += d.power.power_watts;
    }
    if(count) { avg_util/=count; avg_power/=count; }

    std::string hostname = history.front().hostname;
    auto now_c = std::chrono::system_clock::now();
    std::time_t now_t = std::chrono::system_clock::to_time_t(now_c);
    char date_buf[64]; std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now_t));

    std::ostringstream html;
    html << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>DSPTOP — NPU/DSP Report</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
<style>
  :root { --bg:#0a0a0a; --fg:#e0e0e0; --dim:#888; --green:#00c853; --yellow:#ffd600; --red:#ff1744; --cyan:#00bcd4; --card:#141414; --border:#222; }
  *{box-sizing:border-box}
  body{margin:0; font-family: 'JetBrains Mono','Cascadia Code','Consolas',monospace; background:var(--bg); color:var(--fg); line-height:1.5;}
  header{padding:28px 24px 16px; border-bottom:1px solid var(--border); background:linear-gradient(180deg, #0f0f0f, #0a0a0a);}
  header h1{margin:0; font-size:22px; letter-spacing:0.5px;}
  header h1 span{color:var(--cyan)}
  header .sub{color:var(--dim); font-size:13px; margin-top:6px}
  .wrap{max-width:1100px; margin:0 auto; padding:20px 24px;}
  .cards{display:grid; grid-template-columns:repeat(auto-fit,minmax(180px,1fr)); gap:14px; margin:18px 0 22px;}
  .card{background:var(--card); border:1px solid var(--border); border-radius:10px; padding:14px 16px;}
  .card .k{font-size:11px; color:var(--dim); text-transform:uppercase; letter-spacing:0.6px}
  .card .v{font-size:22px; font-weight:700; margin-top:4px}
  .card .v small{font-size:13px; color:var(--dim); font-weight:400}
  .grid{display:grid; grid-template-columns:1fr; gap:18px; margin-top:10px;}
  @media(min-width:900px){ .grid{grid-template-columns:1fr 1fr} .span2{grid-column:span 2}}
  .panel{background:var(--card); border:1px solid var(--border); border-radius:12px; padding:16px;}
  .panel h3{margin:0 0 10px; font-size:13px; color:var(--dim); text-transform:uppercase; letter-spacing:0.6px}
  canvas{width:100%!important; height:260px!important}
  table{width:100%; border-collapse:collapse; font-size:13px; margin-top:8px}
  th,td{padding:8px 10px; border-bottom:1px solid var(--border); text-align:left}
  th{color:var(--dim); font-weight:600; font-size:11px; text-transform:uppercase; letter-spacing:0.5px}
  .badge{display:inline-block; padding:2px 7px; border-radius:6px; font-size:11px; font-weight:700}
  .b-green{background:rgba(0,200,83,0.15); color:var(--green); border:1px solid rgba(0,200,83,0.3)}
  .b-yellow{background:rgba(255,214,0,0.15); color:var(--yellow); border:1px solid rgba(255,214,0,0.3)}
  .b-red{background:rgba(255,23,68,0.15); color:var(--red); border:1px solid rgba(255,23,68,0.3)}
  .util{font-family:monospace; white-space:pre}
  footer{color:var(--dim); font-size:12px; text-align:center; padding:20px; border-top:1px solid var(--border); margin-top:24px}
  a{color:var(--cyan)}
</style>
</head>
<body>
<header>
  <h1>▣ DSPTOP <span>AI Accelerator Report</span> <span style="color:var(--dim); font-weight:400; font-size:13px">— No GPU. Ever.</span></h1>
  <div class="sub">)HTML";
    html << "Host <b>" << EscapeJson(hostname) << "</b> · " << date_buf << " · " << history.size() << " samples · interval " << std::fixed << std::setprecision(1) << (interval_sec*1000) << " ms &nbsp;·&nbsp; All data is NPU/DSP only (AN E/Hexagon/Intel NPU)";
    html << R"HTML(</div>
</header>
<div class="wrap">
  <div class="cards">
)HTML";
    // summary cards
    auto badge = [](double v){ if(v>85) return "b-red"; if(v>=60) return "b-yellow"; return "b-green"; };
    html << "    <div class=\"card\"><div class=\"k\">Avg Utilization</div><div class=\"v\">" << std::fixed << std::setprecision(1) << avg_util << " <small>%</small> <span class=\"badge " << badge(avg_util) << "\">" << (avg_util>85?"hot":avg_util>=60?"warm":"idle") << "</span></div></div>\n";
    html << "    <div class=\"card\"><div class=\"k\">Peak Utilization</div><div class=\"v\">" << std::fixed << std::setprecision(1) << max_util << " <small>%</small> <span class=\"badge " << badge(max_util) << "\">" << (max_util>85?"peak hot":max_util>=60?"peak warm":"ok") << "</span></div></div>\n";
    html << "    <div class=\"card\"><div class=\"k\">Avg Power</div><div class=\"v\">" << std::fixed << std::setprecision(1) << avg_power << " <small>W</small></div></div>\n";
    html << "    <div class=\"card\"><div class=\"k\">Peak Power</div><div class=\"v\">" << std::fixed << std::setprecision(1) << max_power << " <small>W</small></div></div>\n";
    html << "    <div class=\"card\"><div class=\"k\">Max Throttle</div><div class=\"v\">" << std::fixed << std::setprecision(1) << max_throttle << " <small>%</small> <span class=\"badge " << (max_throttle>20?"b-red":max_throttle>5?"b-yellow":"b-green") << "\">" << (max_throttle>20?"throttling":max_throttle>5?"warm":"ok") << "</span></div></div>\n";
    html << "  </div>\n";

    // Table per device last sample
    html << "  <div class=\"panel span2\"><h3>Last Sample — Per-Core &amp; Device</h3><table><tr><th>Device / Core</th><th>Util</th><th>MACC</th><th>TOPS</th><th>SRAM</th><th>VMEM</th><th>Power</th><th>Temp</th></tr>\n";
    for(auto& d: history.back().devices){
        std::string row_color = (d.total_util_pct>85?"color:var(--red)":d.total_util_pct>=60?"color:var(--yellow)":"color:var(--green)");
        html << "<tr style=\"font-weight:700; background:rgba(255,255,255,0.02)\"><td>" << EscapeJson(d.device_name) << " <span style=\"color:var(--dim); font-weight:400\">" << (int)d.clock_mhz << " MHz</span></td>";
        html << "<td style=\"" << row_color << "\">" << std::fixed << std::setprecision(1) << d.total_util_pct << "%</td>";
        html << "<td>" << std::fixed << std::setprecision(1) << d.total_util_pct*0.92 << "%</td>";
        html << "<td>-</td>";
        html << "<td>" << std::fixed << std::setprecision(0) << d.memory.sram_util_pct << "%</td>";
        html << "<td>" << std::fixed << std::setprecision(0) << d.memory.vmem_util_pct << "%</td>";
        html << "<td>" << std::fixed << std::setprecision(1) << d.power.power_watts << "/" << d.power.power_limit_watts << " W (" << (int)d.power.envelope_pct << "%)</td>";
        html << "<td>" << (int)d.power.temp_celsius << "°C</td></tr>\n";
        for(auto& c: d.cores){
            std::string cc = (c.utilization_pct>85?"var(--red)":c.utilization_pct>=60?"var(--yellow)":"var(--green)");
            html << "<tr><td style=\"padding-left:22px\">↳ " << EscapeJson(c.core_name) << "</td>";
            html << "<td><span class=\"util\" style=\"color:" << cc << "\">";
            // ascii bar
            int filled = (int)std::round(c.utilization_pct/100*10);
            html << "[";
            for(int i=0;i<10;++i) html << (i<filled?"█":"░");
            html << "] " << std::fixed << std::setprecision(0) << c.utilization_pct << "%</span></td>";
            html << "<td>" << std::fixed << std::setprecision(0) << c.macc_util_pct << "%</td>";
            html << "<td>" << std::fixed << std::setprecision(1) << c.tops_current << "/" << (int)c.tops_peak << "</td>";
            html << "<td>-</td><td>-</td><td>-</td><td>-</td></tr>\n";
        }
    }
    html << "</table></div>\n";

    // Charts grid
    html << R"HTML(  <div class="grid">
    <div class="panel span2"><h3>Utilization over time — total &amp; per-core (%)</h3><canvas id="utilChart"></canvas></div>
    <div class="panel"><h3>Power envelope (W) &amp; Temp (°C)</h3><canvas id="powerChart"></canvas></div>
    <div class="panel"><h3>SRAM / VMEM utilization (%)</h3><canvas id="memChart"></canvas></div>
    <div class="panel"><h3>MACC utilization (%)</h3><canvas id="maccChart"></canvas></div>
    <div class="panel"><h3>TOPS (current) per core</h3><canvas id="topsChart"></canvas></div>
  </div>
  <div class="panel" style="margin-top:18px"><h3>Raw JSON</h3><p style="color:var(--dim); font-size:13px">Embedded for tooling — also saved as <code>profile.json</code> alongside this HTML.</p>
  <pre id="rawjson" style="max-height:240px; overflow:auto; background:#0a0a0a; border:1px solid var(--border); padding:12px; border-radius:8px; font-size:11px; white-space:pre-wrap; word-break:break-all"></pre>
  </div>
</div>
<footer>No GPU. AI silicon only — Hexagon / ANE / Intel NPU / HTP · Generated by <code>dsptop --ci</code> · <span id="footdate"></span></footer>
<script>
const INTERVAL = )HTML";
    html << interval_sec << ";\n";
    html << "const N = " << history.size() << ";\n";
    // Build labels
    html << "const labels = Array.from({length:N}, (_,i)=> (i*INTERVAL).toFixed(1));\n";
    // Build datasets per device
    // We embed arrays as JS literals
    // For simplicity, support first device's per-core (if multiple devices, we create per-device charts stacked)
    html << "const history = ";
    // embed JSON but escaped for JS — we can reuse SnapshotHistoryToJson string, but minify: embed as JS object literal
    // To avoid double-escaping, directly output JSON
    html << SnapshotHistoryToJson(history) << ";\n";
    html << R"JS(
document.getElementById('rawjson').textContent = JSON.stringify(history, null, 2);
document.getElementById('footdate').textContent = new Date().toLocaleString();

function colorFor(v){ if(v>85) return '#ff1744'; if(v>=60) return '#ffd600'; return '#00c853'; }
function devAt(i){ return history.samples[i]?.devices?.[0]; }

// Util total + per-core
{
  const ctx=document.getElementById('utilChart').getContext('2d');
  const nCores = (history.samples[0]?.devices[0]?.cores?.length)||0;
  const datasets=[];
  // total
  datasets.push({label:'Total', data: history.samples.map(s=> s.devices[0]?.total_util_pct||0), borderColor:'#00bcd4', backgroundColor:'rgba(0,188,212,0.15)', tension:0.3, borderWidth:2, pointRadius:0, fill:true});
  // per-core
  const coreColors=['#00c853','#ffd600','#ff6d00','#ff1744','#7c4dff','#18ffff','#69f0ae','#ff8a80'];
  for(let c=0;c<nCores;c++){
    datasets.push({label: history.samples[0].devices[0].cores[c].name || ('Core '+c), data: history.samples.map(s=> s.devices[0].cores[c]?.util||0), borderColor: coreColors[c%coreColors.length], tension:0.3, borderWidth:1.5, pointRadius:0, borderDash: c? [4,3]: []});
  }
  new Chart(ctx,{type:'line', data:{labels, datasets}, options:{responsive:true, maintainAspectRatio:false, interaction:{mode:'index', intersect:false},
    scales:{x:{title:{display:true,text:'Time (s)'}}, y:{min:0,max:100,title:{display:true,text:'%'}}},
    plugins:{legend:{position:'bottom', labels:{boxWidth:12, font:{size:11}}}} }});
}
// Power + temp
{
  const ctx=document.getElementById('powerChart').getContext('2d');
  new Chart(ctx,{type:'line', data:{labels, datasets:[
    {label:'Power (W)', data: history.samples.map(s=> s.devices[0]?.power_watts||0), yAxisID:'y', borderColor:'#00c853', backgroundColor:'rgba(0,200,83,0.18)', tension:0.3, fill:true, pointRadius:0, borderWidth:2},
    {label:'Envelope %', data: history.samples.map(s=> s.devices[0]?.envelope_pct||0), yAxisID:'y1', borderColor:'#7c4dff', borderDash:[6,3], tension:0.3, pointRadius:0, borderWidth:1.5},
    {label:'Temp °C', data: history.samples.map(s=> s.devices[0]?.temp_c||0), yAxisID:'y1', borderColor:'#ff6d00', tension:0.3, pointRadius:0, borderWidth:1.5}
  ]}, options:{responsive:true, maintainAspectRatio:false, interaction:{mode:'index', intersect:false},
    scales:{x:{title:{display:true,text:'Time (s)'}}, y:{position:'left', title:{display:true,text:'W'}}, y1:{position:'right', min:0, max:100, grid:{drawOnChartArea:false}, title:{display:true,text:'% / °C'}}},
    plugins:{legend:{position:'bottom', labels:{boxWidth:12, font:{size:11}}}}
  }});
}
// Mem
{
  const ctx=document.getElementById('memChart').getContext('2d');
  new Chart(ctx,{type:'line', data:{labels, datasets:[
    {label:'SRAM %', data: history.samples.map(s=> s.devices[0]?.sram_util_pct||0), borderColor:'#00c853', backgroundColor:'rgba(0,200,83,0.15)', tension:0.3, fill:true, pointRadius:0},
    {label:'VMEM %', data: history.samples.map(s=> s.devices[0]?.vmem_util_pct||0), borderColor:'#ffd600', backgroundColor:'rgba(255,214,0,0.13)', tension:0.3, fill:true, pointRadius:0},
    {label:'Throttle %', data: history.samples.map(s=> s.devices[0]?.thermal_throttle_pct||0), borderColor:'#ff1744', borderDash:[4,3], tension:0.3, pointRadius:0}
  ]}, options:{responsive:true, maintainAspectRatio:false, interaction:{mode:'index', intersect:false},
    scales:{x:{title:{display:true,text:'Time (s)'}}, y:{min:0,max:100, title:{display:true,text:'%'}}},
    plugins:{legend:{position:'bottom'}}
  }});
}
// MACC
{
  const ctx=document.getElementById('maccChart').getContext('2d');
  const nCores=(history.samples[0]?.devices[0]?.cores?.length)||0;
  const coreColors=['#00c853','#ffd600','#ff6d00','#ff1744','#7c4dff','#18ffff'];
  const datasets=[];
  for(let c=0;c<nCores;c++){
    datasets.push({label: (history.samples[0].devices[0].cores[c].name||('Core '+c))+' MACC', data: history.samples.map(s=> s.devices[0].cores[c]?.macc||0), borderColor: coreColors[c%coreColors.length], tension:0.3, pointRadius:0, borderWidth:1.6});
  }
  if(!datasets.length) datasets.push({label:'MACC', data: history.samples.map(s=> s.devices[0]?.total_util_pct*0.9||0), borderColor:'#00c853', tension:0.3, pointRadius:0});
  new Chart(ctx,{type:'line', data:{labels,datasets}, options:{responsive:true, maintainAspectRatio:false, interaction:{mode:'index', intersect:false},
    scales:{x:{title:{display:true,text:'Time (s)'}}, y:{min:0,max:100, title:{display:true,text:'%'}}},
    plugins:{legend:{position:'bottom', labels:{boxWidth:12, font:{size:11}}}}
  }});
}
// TOPS
{
  const ctx=document.getElementById('topsChart').getContext('2d');
  const nCores=(history.samples[0]?.devices[0]?.cores?.length)||0;
  const coreColors=['#00bcd4','#7c4dff','#ff6d00','#00c853'];
  const datasets=[];
  for(let c=0;c<nCores;c++){
    datasets.push({label: (history.samples[0].devices[0].cores[c].name||('Core '+c))+' TOPS', data: history.samples.map(s=> s.devices[0].cores[c]?.tops||0), borderColor: coreColors[c%coreColors.length], backgroundColor: coreColors[c%coreColors.length]+'22', tension:0.3, fill:true, pointRadius:0});
  }
  new Chart(ctx,{type:'line', data:{labels,datasets}, options:{responsive:true, maintainAspectRatio:false, interaction:{mode:'index', intersect:false},
    scales:{x:{title:{display:true,text:'Time (s)'}}, y:{title:{display:true,text:'TOPS'}}},
    plugins:{legend:{position:'bottom'}}
  }});
}
)JS";
    html << "\n</script>\n</body></html>\n";
    return html.str();
}

bool WriteHtmlReport(const std::vector<SystemSnapshot>& history, const std::string& path) {
    std::string html = SnapshotHistoryToHtml(history);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << html;
    return out.good();
}

int RunHeadless(const CIOptions& opts) {
    hal::HALManager hal;
    if(!hal.Init()){
        std::cerr << "{\"error\":\"HAL init failed\"}\n";
        return 1;
    }
    std::vector<SystemSnapshot> history;
    history.reserve(opts.duration_seconds * 1000 / opts.sample_interval_ms + 1);

    auto start = std::chrono::steady_clock::now();
    auto end = start + std::chrono::seconds(opts.duration_seconds);

    std::cerr << "[dsptop:ci] sampling for " << opts.duration_seconds << "s, interval "
              << opts.sample_interval_ms << "ms -> " << opts.output_path << "\n";
    std::cerr << "[dsptop:ci] backends: ";
    for(auto& n: hal.BackendNames()) std::cerr << n << " ";
    std::cerr << "\n";

    while(std::chrono::steady_clock::now() < end){
        auto snap = hal.PollAll();
        history.push_back(snap);
        // Live line for logs (no ncurses)
        if(!history.empty()){
            auto& d = snap.devices.front();
            std::cerr << "[sample " << history.size() << "] "
                      << d.device_name << " util=" << std::fixed<<std::setprecision(1)<<d.total_util_pct << "%"
                      << " power="<<d.power.power_watts<<"W"
                      << " throttle="<<d.power.thermal_throttle_pct<<"%\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(opts.sample_interval_ms));
    }

    std::string json = SnapshotHistoryToJson(history);
    if(opts.json_stdout){
        std::cout << json;
    } else {
        std::ofstream out(opts.output_path);
        if(!out){ std::cerr << "Failed to open " << opts.output_path << "\n"; return 1; }
        out << json;
        std::cout << json; // also echo
    }
    // Auto-generate HTML report with graphs alongside JSON
    std::string html_path = opts.html_path;
    if (html_path.empty()) {
        // derive: profile.json -> profile.html, profile.html stays, profile -> profile.html
        auto pos = opts.output_path.rfind('.');
        if (pos != std::string::npos) {
            std::string ext = opts.output_path.substr(pos);
            if (ext == ".html" || ext == ".htm") html_path = opts.output_path;
            else html_path = opts.output_path.substr(0,pos) + ".html";
        } else {
            html_path = opts.output_path + ".html";
        }
        // if json_stdout and no explicit html, default to profile.html
        if (opts.json_stdout && opts.output_path=="profile.json") html_path = "profile.html";
    }
    if (!html_path.empty() && !history.empty()) {
        if (WriteHtmlReport(history, html_path)) {
            std::cerr << "[dsptop:ci] html report -> " << html_path << " (" << history.size() << " samples, open in browser)\n";
        } else {
            std::cerr << "[dsptop:ci] failed to write html " << html_path << "\n";
        }
    }
    std::cerr << "[dsptop:ci] done. samples=" << history.size() << "\n";
    // Check throttle threshold if needed (caller may also check)
    double max_throttle = 0;
    for(auto& s: history) for(auto& d: s.devices) max_throttle = std::max(max_throttle, d.power.thermal_throttle_pct);
    if(max_throttle > 20.0){
        std::cerr << "[dsptop:ci] WARNING: thermal throttle " << max_throttle << "% exceeds 20%\n";
    }
    return 0;
}

} // namespace ci
} // namespace dsptop
