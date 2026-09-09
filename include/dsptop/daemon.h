#pragma once
#include "types.h"
#include <string>
#include <atomic>
#include <mutex>

namespace dsptop {
namespace daemon {

struct DaemonConfig {
    std::string socket_path = "/tmp/dsptop.sock";  // Linux/macOS Unix Domain Socket
    std::string pipe_name = "\\\\.\\pipe\\dsptop"; // Windows Named Pipe
    std::string bind_addr = "127.0.0.1";           // TCP fallback
    int prometheus_port = 9099;                     // /metrics
    int poll_interval_ms = 500;
};

// dsptopd: background sampler exposing metrics via IPC + Prometheus
class Daemon {
public:
    explicit Daemon(DaemonConfig cfg);
    ~Daemon();
    int Run();  // blocking
    void Stop();

private:
    void PollLoop();
    void IPCServerLoop();
    void PrometheusLoop();
    std::string MetricsToPrometheus() const;

    DaemonConfig cfg_;
    std::atomic<bool> running_{false};
    SystemSnapshot latest_;
    mutable std::mutex mu_;
};

// Simple client to query daemon (used by `dsptop --attach`)
SystemSnapshot QueryDaemon(const std::string& endpoint = "");

} // namespace daemon
} // namespace dsptop
