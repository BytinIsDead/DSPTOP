#pragma once
#include "types.h"
#include <string>

namespace dsptop {
namespace ci {

struct CIOptions {
    int duration_seconds = 30;
    std::string output_path = "profile.json";
    std::string html_path = ""; // if empty, auto-derive from output_path (.json -> .html)
    int sample_interval_ms = 500;
    bool json_stdout = false;
};

// Headless mode: poll HAL at interval, write JSON, no ncurses.
// Returns 0 on success, non-zero on thermal > threshold etc.
int RunHeadless(const CIOptions& opts);

// JSON serialization helpers
std::string SnapshotToJson(const SystemSnapshot& snap);
std::string SnapshotHistoryToJson(const std::vector<SystemSnapshot>& history);

// HTML report helpers (Chart.js)
std::string SnapshotHistoryToHtml(const std::vector<SystemSnapshot>& history);
bool WriteHtmlReport(const std::vector<SystemSnapshot>& history, const std::string& path);

} // namespace ci
} // namespace dsptop
