#include "dsptop/tui.h"
#include "dsptop/hal.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <conio.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#endif

namespace dsptop {
namespace tui {

// ---- helpers ----
std::string RenderBar(double pct, int width, bool with_color) {
    pct = std::clamp(pct, 0.0, 100.0);
    int filled = static_cast<int>(std::round(pct / 100.0 * width));
    std::string bar;
    bar.reserve(width+12);
    if (with_color) bar += color::ForUtilization(pct);
    bar += "[";
    for (int i=0;i<width;++i) bar += (i < filled) ? "█" : "░";
    bar += "]";
    if (with_color) bar += color::RESET;
    char pctbuf[16];
    snprintf(pctbuf, sizeof(pctbuf), " %3.0f%%", pct);
    bar += pctbuf;
    return bar;
}

// ---- Terminal ----
Terminal::Terminal() {}
Terminal::~Terminal() { Shutdown(); }

bool Terminal::IsTTY() const {
#ifdef _WIN32
    return is_tty_;
#else
    return isatty(STDOUT_FILENO);
#endif
}

bool Terminal::Init() {
#ifdef _WIN32
    hOut_ = GetStdHandle(STD_OUTPUT_HANDLE);
    hIn_  = GetStdHandle(STD_INPUT_HANDLE);
    is_tty_ = (hOut_ != INVALID_HANDLE_VALUE);
    if (!is_tty_) return false;
    DWORD outMode=0, inMode=0;
    GetConsoleMode(hOut_, &outMode);
    GetConsoleMode(hIn_, &inMode);
    origOutMode_ = outMode;
    origInMode_ = inMode;
    // Enable Virtual Terminal Processing + Disable newline auto-return weirdness
    outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
    inMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(hOut_, outMode);
    // Switch to alternate buffer
    std::cout << "\x1b[?1049h" << std::flush;
    QuerySize();
    HideCursor();
    return true;
#else
    is_tty_ = isatty(STDOUT_FILENO);
    if (!is_tty_) return false;
    // Save termios
    orig_termios_ = new ::termios;
    tcgetattr(STDIN_FILENO, static_cast<struct termios*>(orig_termios_));
    ::termios raw = *static_cast<struct termios*>(orig_termios_);
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    // Alt buffer + hide cursor
    std::cout << "\x1b[?1049h\x1b[?25l" << std::flush;
    alt_buffer_active_ = true;
    QuerySize();
    return true;
#endif
}

void Terminal::Shutdown() {
#ifdef _WIN32
    if (hOut_) {
        std::cout << "\x1b[?1049l" << std::flush;
        ShowCursor();
        SetConsoleMode(hOut_, origOutMode_);
        SetConsoleMode(hIn_, origInMode_);
        hOut_=nullptr;
    }
#else
    if (orig_termios_) {
        tcsetattr(STDIN_FILENO, TCSANOW, static_cast<struct termios*>(orig_termios_));
        delete static_cast<struct termios*>(orig_termios_); orig_termios_=nullptr;
    }
    if (alt_buffer_active_) {
        std::cout << "\x1b[?1049l\x1b[?25h" << std::flush;
        alt_buffer_active_=false;
    }
#endif
}

void Terminal::QuerySize() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO ci{};
    if (GetConsoleScreenBufferInfo(hOut_, &ci)) {
        width_ = ci.srWindow.Right - ci.srWindow.Left + 1;
        height_ = ci.srWindow.Bottom - ci.srWindow.Top + 1;
    }
#else
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)==0 && ws.ws_col>0) {
        width_=ws.ws_col; height_=ws.ws_row;
    }
#endif
}

void Terminal::Clear() { std::cout << "\x1b[2J\x1b[H"; }
void Terminal::MoveTo(int r,int c) { std::cout << "\x1b[" << r << ";" << c << "H"; }
void Terminal::HideCursor() { std::cout << "\x1b[?25l"; }
void Terminal::ShowCursor() { std::cout << "\x1b[?25h"; }

// ---- RenderFrame ----
std::string RenderFrame(const SystemSnapshot& snap, int term_width) {
    std::ostringstream out;
    // Header
    out << color::BOLD << color::CYAN;
    out << "┌─ DSPTOP " << color::DIM << "v0.1.0" << color::RESET << color::BOLD << color::CYAN;
    std::string title = " AI-ONLY ACCELERATOR DASHBOARD ";
    int fill = term_width - 14 - (int)title.size();
    if (fill < 0) fill = 0;
    out << title;
    for(int i=0;i<fill;++i) out << "─";
    out << "┐" << color::RESET << "\n";

    // For each device
    for (auto& dev : snap.devices) {
        out << color::BOLD << "│ " << dev.device_name;
        out << color::DIM << "  (" << dev.clock_mhz << " MHz)" << color::RESET << "\n";

        // Per-core bars
        for (auto& core : dev.cores) {
            out << "│  " << std::left << std::setw(16) << core.core_name << " ";
            out << RenderBar(core.utilization_pct, 10, true);
            out << color::DIM << "  MACC " << RenderBar(core.macc_util_pct, 8, false);
            char tops[32]; snprintf(tops,sizeof(tops)," %.1f/%.0f TOPS", core.tops_current, core.tops_peak);
            out << tops << color::RESET << "\n";
        }
        // SRAM / VMEM
        out << "│  SRAM: " << RenderBar(dev.memory.sram_util_pct, 16, true);
        out << color::DIM << " (" << dev.memory.sram_used_kb << "/" << dev.memory.sram_total_kb << " KB)" << color::RESET;
        out << "  VMEM: " << RenderBar(dev.memory.vmem_util_pct, 12, true) << "\n";

        // Power envelope
        out << "│  Power: " << RenderBar(dev.power.envelope_pct, 16, true);
        char pwbuf[64]; snprintf(pwbuf,sizeof(pwbuf)," %.1f/%.1f W  %.0f°C", dev.power.power_watts, dev.power.power_limit_watts, dev.power.temp_celsius);
        out << pwbuf;
        if (dev.power.throttling) out << color::RED << "  ⚠ THROTTLING" << color::RESET;
        if (dev.power.thermal_throttle_pct > 1.0) {
            char th[32]; snprintf(th,sizeof(th)," (throttle %.0f%%)", dev.power.thermal_throttle_pct);
            out << color::YELLOW << th << color::RESET;
        }
        out << "\n";
        out << color::DIM << "│" << color::RESET << "\n";
    }

    if (snap.devices.empty()) {
        out << "│  " << color::YELLOW << "No AI accelerator detected — showing mock data" << color::RESET << "\n";
    }

    // Footer
    out << color::DIM << "├";
    for(int i=0;i<term_width-2;++i) out << "─";
    out << "┤" << color::RESET << "\n";
    out << color::DIM << "│ q:quit  c:clear  r:reset counters  " << color::RESET;
    // timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    char tbuf[32]; std::strftime(tbuf,sizeof(tbuf),"%H:%M:%S", std::localtime(&tt));
    out << color::DIM << "  " << tbuf << color::RESET;
    // pad
    int used = 34 + 9; // approx
    int pad = term_width - used - 2;
    for(int i=0;i<pad;++i) out << " ";
    out << color::DIM << "│" << color::RESET << "\n";
    out << color::CYAN << "└";
    for(int i=0;i<term_width-2;++i) out << "─";
    out << "┘" << color::RESET << "\n";
    out << color::DIM << "  No GPU. AI silicon only. (Hexagon / ANE / Intel NPU / HTP)" << color::RESET << "\n";
    return out.str();
}

// ---- Dashboard live loop ----
Dashboard::Dashboard(int refresh_ms) : refresh_ms_(refresh_ms) {}
Dashboard::~Dashboard() { Stop(); }

void Dashboard::Stop() {
    running_ = false;
}

int Dashboard::Run() {
    hal::HALManager hal;
    if (!hal.Init()) {
        std::cerr << "HAL init failed\n";
        return 1;
    }
    term_.Init();
    running_ = true;

    // Poll thread
    std::thread poll_th([this, &hal](){
        while(running_) {
            auto snap = hal.PollAll();
            {
                std::lock_guard<std::mutex> lk(latest_mu_);
                latest_ = snap;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(refresh_ms_));
        }
    });

    // Render loop on main thread (handles input)
    // Non-blocking input: sample stdin
#ifdef _WIN32
    // Windows: use _kbhit
    while (running_) {
        SystemSnapshot copy;
        {
            std::lock_guard<std::mutex> lk(latest_mu_);
            copy = latest_;
        }
        term_.Clear();
        std::cout << RenderFrame(copy, term_.Width()) << std::flush;

        // Input poll ~ refresh interval but check key
        for(int i=0;i<refresh_ms_/50;++i){
            if (_kbhit()) {
                int ch = _getch();
                if (ch=='q' || ch=='Q') { running_=false; break; }
                if (ch=='c' || ch=='C') term_.Clear();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (!running_) break;
        }
    }
#else
    // POSIX: select on stdin
    while (running_) {
        SystemSnapshot copy;
        {
            std::lock_guard<std::mutex> lk(latest_mu_);
            copy = latest_;
        }
        term_.Clear();
        std::cout << RenderFrame(copy, term_.Width()) << std::flush;
        // Sleep with input check
        fd_set rfds; FD_ZERO(&rfds); FD_SET(STDIN_FILENO, &rfds);
        struct timeval tv{0, refresh_ms_*1000};
        // Use select timeout = refresh interval
        // We do short poll to keep UI responsive
        int ret = select(STDIN_FILENO+1, &rfds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
            char ch; if (read(STDIN_FILENO,&ch,1)==1) {
                if (ch=='q' || ch=='Q') running_=false;
                if (ch=='c' || ch=='C') term_.Clear();
            }
        }
    }
#endif

    running_ = false;
    if (poll_th.joinable()) poll_th.join();
    term_.Shutdown();
    return 0;
}

} // namespace tui
} // namespace dsptop
