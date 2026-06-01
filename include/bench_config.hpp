#pragma once

#include <cstddef>
#include <cstdint>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>

/// Configuration mirrors RdmaStreamConfig so parameters are directly comparable
/// with the Inflare RDMA benchmark.
struct BenchConfig {
    // ── Network ───────────────────────────────────────────────────────────────
    std::string server_addr = "127.0.0.1";
    uint16_t    port        = 5556;  // Different from Inflare's default 5555

    // ── Buffer parameters (match RdmaStreamConfig) ────────────────────────────
    std::size_t buffer_size  = 1ULL * 1024 * 1024;   // 1 MiB
    uint32_t    num_buffers  = 64;

    // ── Experiment control ────────────────────────────────────────────────────
    double   warmup_sec   = 2.0;
    double   duration_sec = 10.0;
    uint32_t latency_iters = 10'000;  // ping-pong rounds for latency

    // ── Modes ─────────────────────────────────────────────────────────────────
    bool zero_copy  = false;   // use MSG_ZEROCOPY (requires Linux 4.14+, SO_ZEROCOPY)
    bool ping_pong  = false;   // latency measurement mode
    bool csv_output = false;   // machine-readable output
};

inline BenchConfig parse_args(int argc, char** argv)
{
    BenchConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        auto next = [&]() -> std::string_view {
            if (++i >= argc) throw std::runtime_error(std::string("missing value for ") + std::string(a));
            return argv[i];
        };

        if      (a == "--addr"      || a == "-a")  cfg.server_addr    = next();
        else if (a == "--port"      || a == "-p")  cfg.port           = static_cast<uint16_t>(std::stoul(std::string(next())));
        else if (a == "--buf-size"  || a == "-b")  cfg.buffer_size    = std::stoull(std::string(next()));
        else if (a == "--num-buf"   || a == "-n")  cfg.num_buffers    = static_cast<uint32_t>(std::stoul(std::string(next())));
        else if (a == "--duration"  || a == "-d")  cfg.duration_sec   = std::stod(std::string(next()));
        else if (a == "--warmup"    || a == "-w")  cfg.warmup_sec     = std::stod(std::string(next()));
        else if (a == "--iters"     || a == "-i")  cfg.latency_iters  = static_cast<uint32_t>(std::stoul(std::string(next())));
        else if (a == "--zero-copy" || a == "-z")  cfg.zero_copy      = true;
        else if (a == "--ping-pong" || a == "-l")  cfg.ping_pong      = true;
        else if (a == "--csv")                     cfg.csv_output     = true;
        else {
            std::println(stderr, "Unknown option: {}", a);
            throw std::runtime_error("bad args");
        }
    }
    return cfg;
}

inline void print_config(const BenchConfig& cfg)
{
    std::println("  server_addr   : {}", cfg.server_addr);
    std::println("  port          : {}", cfg.port);
    std::println("  buffer_size   : {} KiB", cfg.buffer_size / 1024);
    std::println("  num_buffers   : {}", cfg.num_buffers);
    std::println("  warmup        : {:.1f} s", cfg.warmup_sec);
    std::println("  duration      : {:.1f} s", cfg.duration_sec);
    std::println("  zero_copy     : {}", cfg.zero_copy);
    std::println("  ping_pong     : {}", cfg.ping_pong);
}
