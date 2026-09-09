#include "dsptop/hal.h"
#include "dsptop/tui.h"
#include "dsptop/ci.h"
#include "dsptop/daemon.h"
#include <iostream>
#include <string>
#include <vector>

static void PrintHelp(const char* prog){
    std::cout << "DSPTOP — AI Accelerator Terminal Dashboard (No GPU. Ever.)\n"
              << "Usage: " << prog << " [options]\n"
              << "  --ci                    Headless CI mode (no TUI, JSON output)\n"
              << "  --duration <time>       CI duration (e.g., 30s, 1m) [default 30s]\n"
              << "  --output <file>         JSON output path [default profile.json]\n"
              << "  --interval <ms>         Sample interval ms [default 500]\n"
              << "  --daemon                Run as background daemon (dsptopd)\n"
              << "  --prometheus-port <n>   Prometheus port [default 9099]\n"
              << "  --help                  Show help\n"
              << "  --version               Show version\n";
}

int main(int argc, char** argv){
    std::vector<std::string> args(argv+1, argv+argc);
    auto has = [&](const std::string& f){ return std::find(args.begin(), args.end(), f)!=args.end(); };
    auto get = [&](const std::string& f, const std::string& def)->std::string{
        auto it = std::find(args.begin(), args.end(), f);
        if(it!=args.end() && it+1!=args.end()) return *(it+1);
        return def;
    };

    if(has("--help")||has("-h")){ PrintHelp(argv[0]); return 0; }
    if(has("--version")){ std::cout<<"dsptop 0.1.0\n"; return 0; }

    if(has("--daemon") || has("--daemonize")){
        dsptop::daemon::DaemonConfig cfg;
        cfg.prometheus_port = std::stoi(get("--prometheus-port","9099"));
        dsptop::daemon::Daemon d(cfg);
        return d.Run();
    }

    if(has("--ci")){
        dsptop::ci::CIOptions opts;
        std::string dur = get("--duration","30s");
        // parse e.g. 30s, 1m
        int secs = 30;
        try {
            if(!dur.empty() && dur.back()=='s') secs = std::stoi(dur.substr(0,dur.size()-1));
            else if(!dur.empty() && dur.back()=='m') secs = std::stoi(dur.substr(0,dur.size()-1))*60;
            else secs = std::stoi(dur);
        } catch(...) { secs=30; }
        opts.duration_seconds = secs;
        opts.output_path = get("--output", get("--out","profile.json"));
        opts.sample_interval_ms = std::stoi(get("--interval","500"));
        // also support --ci --duration 30s --output profile.json as called in workflow
        return dsptop::ci::RunHeadless(opts);
    }

    // Default: interactive TUI
    dsptop::tui::Dashboard dash(500);
    return dash.Run();
}
