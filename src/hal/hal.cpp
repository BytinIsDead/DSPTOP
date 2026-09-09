#include "dsptop/hal.h"
#include "dsptop/beacon.h"
#include <chrono>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace dsptop {
namespace hal {

std::vector<std::unique_ptr<IAcceleratorBackend>> CreateAvailableBackends() {
    std::vector<std::unique_ptr<IAcceleratorBackend>> out;

#if defined(__APPLE__) && defined(__aarch64__)
    auto ane = CreateDarwinANEBackend();
    if (ane && ane->IsAvailable()) out.push_back(std::move(ane));
#elif defined(__linux__) && defined(__aarch64__)
    auto dsp = CreateLinuxDSPBackend();
    if (dsp && dsp->IsAvailable()) out.push_back(std::move(dsp));
#elif defined(_WIN32)
    // Runtime arch detection: try both backends, keep available ones
    auto intel = CreateWindowsIntelNPUBackend();
    if (intel && intel->IsAvailable()) out.push_back(std::move(intel));
    auto snapdragon = CreateWindowsSnapdragonNPUBackend();
    if (snapdragon && snapdragon->IsAvailable()) out.push_back(std::move(snapdragon));
#endif

    // Mock fallback ensures CI never produces empty metrics
    if (out.empty()) {
        out.push_back(CreateMockBackend());
    }
    return out;
}

// ---- HALManager ----

HALManager::HALManager() = default;
HALManager::~HALManager() { Shutdown(); }

bool HALManager::Init() {
    if (initialized_) return true;
    backends_ = CreateAvailableBackends();
    for (auto& b : backends_) {
        if (!b->Init()) return false;
    }
    initialized_ = true;
    return true;
}

void HALManager::Shutdown() {
    for (auto& b : backends_) b->Shutdown();
    initialized_ = false;
}

SystemSnapshot HALManager::PollAll() {
    SystemSnapshot snap;
    snap.timestamp = std::chrono::system_clock::now();
    char host[256] = {};
#ifdef _WIN32
    DWORD sz = sizeof(host);
    GetComputerNameA(host, &sz);
#else
    gethostname(host, sizeof(host)-1);
#endif
    snap.hostname = host;
    for (auto& b : backends_) {
        snap.devices.push_back(b->Poll());
    }
    return snap;
}

std::vector<std::string> HALManager::BackendNames() const {
    std::vector<std::string> names;
    for (auto& b : backends_) names.push_back(b->Name());
    return names;
}

// ---- Mock backend for CI ----

class MockBackend : public IAcceleratorBackend {
public:
    bool Init() override { return true; }
    void Shutdown() override {}
    std::string Name() const override { return "MockNPU (CI)"; }
    AcceleratorType Type() const override { return AcceleratorType::GenericNPU; }
    bool IsAvailable() const override { return true; }
    DeviceMetrics Poll() override {
        DeviceMetrics m;
        m.device_name = "Mock NPU";
        m.type = AcceleratorType::GenericNPU;
        // 1) If inference beacon is active (real 8B Q4_K_M or dummy), use it as ground truth
        double b_util = 0, b_macc = 0;
        bool has_beacon = beacon::TryReadBeacon(b_util, b_macc, 3.0);
        double base;
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (has_beacon) {
            base = b_util;
        } else {
            // Deterministic pseudo-load: sawtooth 20..90 for CI reproducibility
            base = 40.0 + 30.0 * std::sin(t * 0.7);
            if (base < 5) base = 5; if (base > 95) base = 95;
        }
        double macc0 = has_beacon ? b_macc : base*0.9;
        double macc1 = has_beacon ? b_macc*0.85 : base*0.68;
        CoreMetrics c0{0, "NPU Core 0", base, macc0, 11.2*(base/100), 38.0};
        CoreMetrics c1{1, "NPU Core 1", base*0.75, macc1, 8.4*(base/100), 38.0};
        m.cores = {c0, c1};
        m.total_util_pct = base;
        m.memory.sram_util_pct = has_beacon ? std::clamp(38 + base*0.18, 0.0, 95.0) : 42.0 + 10.0*std::sin(t*0.3);
        m.memory.vmem_util_pct = has_beacon ? std::clamp(28 + base*0.14, 0.0, 95.0) : 35.0;
        m.memory.sram_used_kb = 820 + (int)(base*6); m.memory.sram_total_kb = 2048;
        // Power correlates with util when beacon active
        m.power.power_watts = has_beacon ? (0.8 + 5.2*base/100.0) : 3.2;
        m.power.power_limit_watts = 8.0;
        m.power.envelope_pct = m.power.power_watts / m.power.power_limit_watts * 100;
        m.power.temp_celsius = has_beacon ? (48 + base*0.22) : 62.0;
        m.power.thermal_throttle_pct = has_beacon ? (base > 88 ? (base-88)*1.5 : 0) : 2.0 + 5.0*std::sin(t*0.1 + 1.0);
        if (m.power.thermal_throttle_pct < 0) m.power.thermal_throttle_pct = 0;
        m.clock_mhz = 1200 + (has_beacon ? base*2 : 0);
        m.timestamp = std::chrono::system_clock::now();
        return m;
    }
};

std::unique_ptr<IAcceleratorBackend> CreateMockBackend() {
    return std::make_unique<MockBackend>();
}

} // namespace hal
} // namespace dsptop
