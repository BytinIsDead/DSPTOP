#include "dsptop/daemon.h"
#include "dsptop/hal.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

namespace dsptop {
namespace daemon {

Daemon::Daemon(DaemonConfig cfg) : cfg_(std::move(cfg)) {}
Daemon::~Daemon() { Stop(); }

void Daemon::Stop() { running_ = false; }

void Daemon::PollLoop() {
    hal::HALManager hal;
    hal.Init();
    while(running_){
        auto snap = hal.PollAll();
        {
            std::lock_guard<std::mutex> lk(mu_);
            latest_ = snap;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.poll_interval_ms));
    }
}

std::string Daemon::MetricsToPrometheus() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::ostringstream o;
    o << "# HELP dsptop_npu_utilization NPU/DSP utilization percent\n";
    o << "# TYPE dsptop_npu_utilization gauge\n";
    for(auto& d: latest_.devices){
        std::string label = d.device_name;
        // sanitize
        for(char& c: label) if(c==' ') c='_';
        o << "dsptop_npu_utilization{device=\"" << label << "\"} " << d.total_util_pct << "\n";
        o << "dsptop_npu_power_watts{device=\"" << label << "\"} " << d.power.power_watts << "\n";
        o << "dsptop_npu_envelope_pct{device=\"" << label << "\"} " << d.power.envelope_pct << "\n";
        o << "dsptop_npu_thermal_throttle{device=\"" << label << "\"} " << d.power.thermal_throttle_pct << "\n";
        o << "dsptop_npu_sram_pct{device=\"" << label << "\"} " << d.memory.sram_util_pct << "\n";
        for(auto& c: d.cores){
            o << "dsptop_npu_core_util{device=\"" << label << "\",core=\"" << c.core_id << "\"} " << c.utilization_pct << "\n";
        }
    }
    return o.str();
}

void Daemon::PrometheusLoop() {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(cfg_.prometheus_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(srv, (sockaddr*)&addr, sizeof(addr));
    listen(srv, 4);
    while(running_){
        SOCKET cli = accept(srv, nullptr, nullptr);
        if(cli==INVALID_SOCKET) continue;
        char buf[1024]; recv(cli, buf, sizeof(buf), 0);
        std::string body = MetricsToPrometheus();
        std::ostringstream resp;
        resp << "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "<<body.size()<<"\r\n\r\n"<<body;
        std::string r = resp.str();
        send(cli, r.c_str(), (int)r.size(), 0);
        closesocket(cli);
    }
    closesocket(srv); WSACleanup();
#else
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int opt=1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(cfg_.prometheus_port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(srv,(sockaddr*)&addr,sizeof(addr));
    listen(srv,4);
    while(running_){
        int cli = accept(srv,nullptr,nullptr);
        if(cli<0) continue;
        char buf[1024]; recv(cli,buf,sizeof(buf),0);
        std::string body = MetricsToPrometheus();
        std::ostringstream resp;
        resp<<"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "<<body.size()<<"\r\n\r\n"<<body;
        std::string r=resp.str();
        send(cli,r.c_str(),r.size(),0);
        close(cli);
    }
    close(srv);
#endif
}

void Daemon::IPCServerLoop() {
#ifdef _WIN32
    // Named Pipe: \\.\pipe\dsptop
    while(running_){
        HANDLE hPipe = CreateNamedPipeA(
            cfg_.pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 4096, 4096, 0, nullptr);
        if(hPipe==INVALID_HANDLE_VALUE) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        BOOL connected = ConnectNamedPipe(hPipe, nullptr) ? TRUE : (GetLastError()==ERROR_PIPE_CONNECTED);
        if(connected){
            std::string metrics = MetricsToPrometheus(); // reuse for IPC JSON later
            // For now send Prometheus; client can parse
            DWORD written=0;
            WriteFile(hPipe, metrics.c_str(), (DWORD)metrics.size(), &written, nullptr);
            FlushFileBuffers(hPipe);
        }
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
#else
    // Unix Domain Socket
    unlink(cfg_.socket_path.c_str());
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{}; addr.sun_family=AF_UNIX;
    strncpy(addr.sun_path, cfg_.socket_path.c_str(), sizeof(addr.sun_path)-1);
    bind(srv,(sockaddr*)&addr,sizeof(addr));
    listen(srv,4);
    while(running_){
        int cli = accept(srv,nullptr,nullptr);
        if(cli<0) continue;
        std::string body = MetricsToPrometheus();
        send(cli, body.c_str(), body.size(), 0);
        close(cli);
    }
    close(srv);
    unlink(cfg_.socket_path.c_str());
#endif
}

int Daemon::Run() {
    running_=true;
    std::thread poll_t(&Daemon::PollLoop, this);
    std::thread prom_t(&Daemon::PrometheusLoop, this);
    std::thread ipc_t(&Daemon::IPCServerLoop, this);
    std::cout << "[dsptopd] running. Prometheus http://"<<cfg_.bind_addr<<":"<<cfg_.prometheus_port<<"/metrics\n";
#ifdef _WIN32
    std::cout << "[dsptopd] Named Pipe "<<cfg_.pipe_name<<"\n";
#else
    std::cout << "[dsptopd] UDS "<<cfg_.socket_path<<"\n";
#endif
    poll_t.join();
    prom_t.join();
    ipc_t.join();
    return 0;
}

SystemSnapshot QueryDaemon(const std::string& endpoint) {
    // Minimal stub: returns empty snapshot (client would parse Prometheus)
    (void)endpoint;
    return SystemSnapshot{};
}

} // namespace daemon
} // namespace dsptop
