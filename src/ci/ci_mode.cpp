#include "dsptop/ci.h"
#include "dsptop/hal.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <vector>

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
