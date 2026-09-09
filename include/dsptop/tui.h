#pragma once
#include "types.h"
#include <atomic>
#include <thread>
#include <mutex>

namespace dsptop {
namespace tui {

// ANSI color helpers — used on all platforms when VT is enabled
namespace color {
    constexpr const char* RESET   = "\x1b[0m";
    constexpr const char* GREEN   = "\x1b[32m";
    constexpr const char* YELLOW  = "\x1b[33m";
    constexpr const char* RED     = "\x1b[31m";
    constexpr const char* CYAN    = "\x1b[36m";
    constexpr const char* DIM     = "\x1b[2m";
    constexpr const char* BOLD    = "\x1b[1m";

    inline const char* ForUtilization(double pct) {
        if (pct > 85.0) return RED;
        if (pct >= 60.0) return YELLOW;
        return GREEN;
    }
}

// Cross-platform terminal abstraction
class Terminal {
public:
    Terminal();
    ~Terminal();

    bool Init();        // enable VT / raw mode / alt buffer
    void Shutdown();    // restore
    void Clear();
    void MoveTo(int row, int col);
    void HideCursor();
    void ShowCursor();
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsTTY() const;

private:
    void EnableVTSequence();
    void QuerySize();
    int width_ = 80;
    int height_ = 24;
    bool is_tty_ = false;
#ifdef _WIN32
    void* hOut_ = nullptr;
    void* hIn_ = nullptr;
    unsigned long origOutMode_ = 0;
    unsigned long origInMode_ = 0;
#else
    void* orig_termios_ = nullptr;
    bool alt_buffer_active_ = false;
#endif
};

// Render a single utilization bar: [██████░░░░] 60%
std::string RenderBar(double pct, int width = 10, bool with_color = true);

// Render full frame to string buffer (no side-effects, testable)
std::string RenderFrame(const SystemSnapshot& snap, int term_width = 80);

// Live dashboard: owns HAL polling loop + render loop
class Dashboard {
public:
    explicit Dashboard(int refresh_ms = 500);
    ~Dashboard();
    int Run(); // blocking until q/Ctrl+C
    void Stop();

private:
    void RenderLoop();
    void PollLoop();
    int refresh_ms_;
    std::atomic<bool> running_{false};
    SystemSnapshot latest_;
    std::mutex latest_mu_;
    Terminal term_;
};

} // namespace tui
} // namespace dsptop
