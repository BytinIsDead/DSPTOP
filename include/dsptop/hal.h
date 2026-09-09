#pragma once
#include "types.h"
#include <memory>
#include <stdexcept>

namespace dsptop {
namespace hal {

// Unified Hardware Abstraction Layer interface
// Every vendor backend implements this interface. Zero GPU leakage.
// Thread-safety: Implementations must be safe to call Poll() from a single
// dedicated sampling thread. Init() / Shutdown() are not thread-safe.

class IAcceleratorBackend {
public:
    virtual ~IAcceleratorBackend() = default;

    // Lifecycle
    virtual bool Init() = 0;
    virtual void Shutdown() = 0;

    // Identity
    virtual std::string Name() const = 0;
    virtual AcceleratorType Type() const = 0;
    virtual bool IsAvailable() const = 0;

    // Polling - called at sample interval (default 500ms)
    // Must return fresh snapshot; may block up to 50ms max.
    virtual DeviceMetrics Poll() = 0;

    // Optional: perf counter reset, calibration
    virtual void ResetCounters() {}
};

// Factory: Returns all available backends on this platform.
// On macOS: {DarwinANEBackend}
// On Linux ARM64: {LinuxHexagonBackend} or {GenericSysfsBackend}
// On Windows x64: {WindowsIntelNPUBackend}
// On Windows ARM64: {WindowsSnapdragonNPUBackend}
std::vector<std::unique_ptr<IAcceleratorBackend>> CreateAvailableBackends();

// Platform-specific concrete backends (defined in src/hal/*.cpp)
std::unique_ptr<IAcceleratorBackend> CreateDarwinANEBackend();
std::unique_ptr<IAcceleratorBackend> CreateLinuxDSPBackend();
std::unique_ptr<IAcceleratorBackend> CreateWindowsIntelNPUBackend();
std::unique_ptr<IAcceleratorBackend> CreateWindowsSnapdragonNPUBackend();
std::unique_ptr<IAcceleratorBackend> CreateMockBackend(); // for CI / dev

// Aggregator that merges multiple backends into SystemSnapshot
class HALManager {
public:
    HALManager();
    ~HALManager();

    bool Init();
    void Shutdown();
    SystemSnapshot PollAll();
    std::vector<std::string> BackendNames() const;

private:
    std::vector<std::unique_ptr<IAcceleratorBackend>> backends_;
    bool initialized_ = false;
};

} // namespace hal
} // namespace dsptop
