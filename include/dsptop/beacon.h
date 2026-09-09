#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <regex>

namespace dsptop {
namespace beacon {

// Shared beacon file between inference scripts (Python) and HAL (C++).
// Python writers (infer_q4km.py, dummy_inference.py) update this every ~200ms
// while inference is active: {"ts": 1715000000.123, "util": 78.0, "macc": 72.0, "backend": "openvino_npu"}
// HAL readers check it first; if recent (<3s) they use beacon util as ground truth.
// This makes utilisation CORRECTLY track real 8B Q4_K_M even on CI/mock hardware.

inline std::vector<std::string> BeaconPaths() {
    std::vector<std::string> out;
    // Cross-platform candidates — checked in order
#ifdef _WIN32
    if (auto* t = std::getenv("TEMP")) out.push_back(std::string(t) + "\\dsptop_beacon.json");
    if (auto* t = std::getenv("TMP")) out.push_back(std::string(t) + "\\dsptop_beacon.json");
    out.push_back(".\\dsptop_beacon.json");
    out.push_back("C:\\Temp\\dsptop_beacon.json");
#else
    out.push_back("/tmp/dsptop_beacon.json");
    out.push_back("./dsptop_beacon.json");
    if (auto* h = std::getenv("HOME")) {
        out.push_back(std::string(h) + "/.cache/dsptop_beacon.json");
        out.push_back(std::string(h) + "/tmp/dsptop_beacon.json");
    }
    if (auto* x = std::getenv("XDG_RUNTIME_DIR")) out.push_back(std::string(x) + "/dsptop_beacon.json");
#endif
    return out;
}

// Try to read beacon util. Returns true if recent beacon found (<max_age_sec).
// out_util in [0,100], out_macc optionally.
inline bool TryReadBeacon(double& out_util, double& out_macc, double max_age_sec = 3.0) {
    const auto paths = BeaconPaths();
    const auto now = std::chrono::system_clock::now();
    const double now_sec = std::chrono::duration<double>(now.time_since_epoch()).count();
    for (const auto& path : paths) {
        std::ifstream f(path);
        if (!f) continue;
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (content.empty()) continue;
        // Extract ts and util via regex (no JSON dep)
        std::regex ts_re(R"("ts"\s*:\s*([0-9]+\.?[0-9]*))");
        std::regex util_re(R"("util"\s*:\s*([0-9]+\.?[0-9]*))");
        std::regex macc_re(R"("macc"\s*:\s*([0-9]+\.?[0-9]*))");
        std::smatch m;
        double ts = 0, util = -1, macc = -1;
        if (std::regex_search(content, m, ts_re)) {
            try { ts = std::stod(m[1]); } catch(...) {}
        }
        if (std::regex_search(content, m, util_re)) {
            try { util = std::stod(m[1]); } catch(...) {}
        }
        if (std::regex_search(content, m, macc_re)) {
            try { macc = std::stod(m[1]); } catch(...) {}
        }
        if (util < 0) continue;
        // Check freshness
        double age = now_sec - ts;
        if (ts > 0 && age > max_age_sec) continue; // stale
        // If no ts field, accept if file mtime recent (fallback)
        out_util = std::clamp(util, 0.0, 100.0);
        out_macc = (macc >= 0) ? std::clamp(macc, 0.0, 100.0) : out_util * 0.92;
        return true;
    }
    return false;
}

inline bool TryReadBeacon(double& out_util, double max_age_sec = 3.0) {
    double macc = 0;
    return TryReadBeacon(out_util, macc, max_age_sec);
}

} // namespace beacon
} // namespace dsptop
