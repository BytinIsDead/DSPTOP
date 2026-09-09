#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace dsptop {

enum class AcceleratorType {
    Unknown,
    AppleANE,       // Apple Neural Engine
    HexagonDSP,     // Qualcomm Hexagon
    IntelNPU,       // Intel AI Boost / NPU (Meteor Lake+)
    SnapdragonNPU,  // Snapdragon X Elite NPU (QCOM compute DSP + HTP)
    GenericNPU      // fallback / mock
};

struct CoreMetrics {
    uint32_t core_id = 0;
    std::string core_name;          // e.g. "ANE Core 0", "Hexagon Core 1"
    double utilization_pct = 0.0;   // 0.0 - 100.0
    double macc_util_pct = 0.0;     // Multiply-Accumulate engine load
    double tops_current = 0.0;      // Current TOPS
    double tops_peak = 0.0;         // Peak TOPS for this core
};

struct MemoryMetrics {
    double sram_util_pct = 0.0;     // SRAM / TCM utilization
    double vmem_util_pct = 0.0;     // Vector / Shared memory
    uint64_t sram_used_kb = 0;
    uint64_t sram_total_kb = 0;
    uint64_t vmem_used_kb = 0;
    uint64_t vmem_total_kb = 0;
};

struct PowerMetrics {
    double power_watts = 0.0;
    double power_limit_watts = 0.0;
    double envelope_pct = 0.0;      // power / limit * 100
    double temp_celsius = 0.0;
    double thermal_throttle_pct = 0.0; // 0 - 100, time throttled
    bool throttling = false;
};

struct DeviceMetrics {
    std::string device_name;        // e.g. "Apple ANE", "Hexagon DSP", "Intel NPU"
    AcceleratorType type = AcceleratorType::Unknown;
    std::vector<CoreMetrics> cores;
    MemoryMetrics memory;
    PowerMetrics power;
    double total_util_pct = 0.0;    // aggregate
    std::chrono::system_clock::time_point timestamp;
    uint64_t inference_count = 0;   // completed inferences since boot
    double clock_mhz = 0.0;
};

struct SystemSnapshot {
    std::vector<DeviceMetrics> devices;
    std::chrono::system_clock::time_point timestamp;
    std::string hostname;
    std::string os_version;
};

} // namespace dsptop
