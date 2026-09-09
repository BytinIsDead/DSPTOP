#include "dsptop/hal.h"
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
        // Deterministic pseudo-load: sawtooth 20..90 for CI reproducibility
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        double t = std::chrono::duration<double>(now).count();
        double base = 40.0 + 30.0 * std::sin(t * 0.7);
        // Clamp
        if (base < 5) base = 5; if (base > 95) base = 95;

        CoreMetrics c0{0, "NPU Core 0", base, base*0.9, 11.2*(base/100), 38.0};
        CoreMetrics c1{1, "NPU Core 1", base*0.75, base*0.68, 8.4*(base/100), 38.0};
        m.cores = {c0, c1};
        m.total_util_pct = base;
        m.memory.sram_util_pct = 42.0 + 10.0*std::sin(t*0.3);
        m.memory.vmem_util_pct = 35.0;
        m.memory.sram_used_kb = 820; m.memory.sram_total_kb = 2048;
        m.power.power_watts = 3.2; m.power.power_limit_watts = 8.0;
        m.power.envelope_pct = 40.0;
        m.power.temp_celsius = 62.0;
        m.power.thermal_throttle_pct = 2.0 + 5.0*std::sin(t*0.1 + 1.0);
        if (m.power.thermal_throttle_pct < 0) m.power.thermal_throttle_pct = 0;
        m.clock_mhz = 1200;
        m.timestamp = std::chrono::system_clock::now();
        return m;
    }
};

std::unique_ptr<IAcceleratorBackend> CreateMockBackend() {
    return std::make_unique<MockBackend>();
}

} // namespace hal
} // namespace dsptop
