/**
 * tcp_client.cpp — TCP/IP benchmark client
 *
 * Modes:
 *   default    : throughput — send buffers as fast as possible, report T, S, M, U
 *   --ping-pong: latency    — send one buffer, wait for echo, measure RTT/2
 *   --zero-copy: use MSG_ZEROCOPY for sending (Linux 4.14+)
 *
 * Usage:
 *   ./tcp_bench_client --addr 192.168.1.2 --port 5556 --buf-size 1048576 \
 *                      --num-buf 64 --duration 10
 *   ./tcp_bench_client --addr 192.168.1.2 --ping-pong --iters 10000
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <memory>
#include <print>
#include <vector>

#include "bench_config.hpp"
#include "cpu_monitor.hpp"
#include "latency_tracker.hpp"
#include "metrics.hpp"
#include "socket_utils.hpp"

namespace
{
std::atomic_bool g_stop = false;
void signal_handler(int) { g_stop.store(true); }
}

// ── Throughput mode ───────────────────────────────────────────────────────────

static BenchmarkResult run_throughput_client(int fd, const BenchConfig& cfg)
{
    auto buf = std::make_unique<char[]>(cfg.buffer_size);
    std::memset(buf.get(), 0xAB, cfg.buffer_size);  // fill with pattern

    const int send_flags = cfg.zero_copy ? MSG_ZEROCOPY : 0;

    uint64_t meas_bytes    = 0;
    uint64_t meas_syscalls = 0;

    auto wall_start  = std::chrono::steady_clock::now();
    auto warmup_end  = wall_start + std::chrono::duration<double>(cfg.warmup_sec);
    auto measure_end = warmup_end + std::chrono::duration<double>(cfg.duration_sec);

    bool measuring = false;
    CpuSnapshot cpu_before{};
    std::chrono::steady_clock::time_point meas_start;

    std::println("[Client] Sending (warmup={:.1f}s, measure={:.1f}s, zero_copy={})...",
                 cfg.warmup_sec, cfg.duration_sec, cfg.zero_copy);

    while (!g_stop.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();

        if (!measuring && now >= warmup_end) {
            measuring  = true;
            meas_start = now;
            cpu_before = read_cpu_snapshot();
            std::println("[Client] Warmup done, measuring...");
        }
        if (measuring && now >= measure_end) break;

        if (!send_all(fd, buf.get(), cfg.buffer_size, send_flags)) {
            std::println("[Client] send() failed / connection closed.");
            break;
        }
        if (cfg.zero_copy) drain_zerocopy_completions(fd);

        if (measuring) {
            ++meas_syscalls;
            meas_bytes += cfg.buffer_size;
        }
    }

    CpuSnapshot cpu_after = read_cpu_snapshot();
    double cpu_pct = compute_cpu_util(cpu_before, cpu_after);

    double dur_sec = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - meas_start).count();

    double gbps = (static_cast<double>(meas_bytes) * 8.0) / (dur_sec * 1e9);
    double sps  = static_cast<double>(meas_syscalls) / dur_sec;
    // Sender side: one copy user→kernel (blocking) or zero (MSG_ZEROCOPY)
    double mbw  = cfg.zero_copy ? gbps / 8.0 : gbps * 1.0 / 8.0;

    ThroughputResult thr;
    thr.gbps             = gbps;
    thr.total_bytes      = meas_bytes;
    thr.duration_sec     = dur_sec;
    thr.syscalls         = meas_syscalls;
    thr.syscalls_per_sec = sps;
    thr.mem_bw_gbps      = mbw;

    BenchmarkResult res;
    res.mode        = cfg.zero_copy ? "tcp-zerocopy" : "tcp-blocking";
    res.side        = "client";
    res.buffer_size = cfg.buffer_size;
    res.num_buffers = cfg.num_buffers;
    res.throughput  = thr;
    res.cpu         = CpuResult{cpu_pct};
    return res;
}

// ── Ping-pong (latency) mode ──────────────────────────────────────────────────

static BenchmarkResult run_pingpong_client(int fd, const BenchConfig& cfg)
{
    const std::size_t ping_size = std::min(cfg.buffer_size, std::size_t{65536});
    auto buf = std::make_unique<char[]>(ping_size);
    std::memset(buf.get(), 0xCD, ping_size);

    LatencyTracker tracker;
    tracker.reserve(cfg.latency_iters);

    std::println("[Client] Ping-pong: {} iterations, ping_size={} B...",
                 cfg.latency_iters, ping_size);

    // Warmup ping-pongs (not recorded)
    const uint32_t warmup_iters = 1000;
    for (uint32_t i = 0; i < warmup_iters; ++i) {
        if (!send_all(fd, buf.get(), ping_size)) { std::println("[Client] warmup send failed"); break; }
        if (!recv_all(fd, buf.get(), ping_size)) { std::println("[Client] warmup recv failed"); break; }
    }

    for (uint32_t i = 0; i < cfg.latency_iters && !g_stop; ++i) {
        auto t1 = std::chrono::steady_clock::now();

        if (!send_all(fd, buf.get(), ping_size)) break;
        if (!recv_all(fd, buf.get(), ping_size)) break;

        auto t2 = std::chrono::steady_clock::now();
        double rtt_us = std::chrono::duration<double, std::micro>(t2 - t1).count();
        tracker.record(rtt_us / 2.0);  // one-way latency = RTT / 2
    }

    LatencyResult lat;
    lat.p50_us  = tracker.p50();
    lat.p99_us  = tracker.p99();
    lat.p999_us = tracker.p999();
    lat.mean_us = tracker.mean();
    lat.min_us  = tracker.min();
    lat.max_us  = tracker.max();
    lat.samples = static_cast<uint32_t>(tracker.count());

    BenchmarkResult res;
    res.mode        = cfg.zero_copy ? "tcp-zerocopy" : "tcp-blocking";
    res.side        = "client";
    res.buffer_size = cfg.buffer_size;
    res.num_buffers = cfg.num_buffers;
    res.latency     = lat;
    return res;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    std::println("==========================================");
    std::println("   TCP/IP Benchmark — Client");
    std::println("==========================================");

    BenchConfig cfg;
    try { cfg = parse_args(argc, argv); }
    catch (const std::exception& e) {
        std::println(stderr, "Error: {}", e.what());
        std::println(stderr, "Usage: {} --addr IP [--port N] [--buf-size B]"
                             " [--num-buf N] [--duration S] [--ping-pong]"
                             " [--zero-copy] [--csv]", argv[0]);
        return 1;
    }

    print_config(cfg);

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::println("[Client] Connecting to {}:{}...", cfg.server_addr, cfg.port);
    int fd = connect_to_server(cfg.server_addr, cfg.port);
    configure_socket(fd, cfg.buffer_size);

    if (cfg.zero_copy) {
        if (enable_zero_copy(fd))
            std::println("[Client] MSG_ZEROCOPY enabled.");
        else
            std::println("[Client] WARNING: SO_ZEROCOPY not supported, falling back to copy.");
    }

    BenchmarkResult result = cfg.ping_pong
                                 ? run_pingpong_client(fd, cfg)
                                 : run_throughput_client(fd, cfg);

    ::close(fd);

    if (cfg.csv_output)
        print_result_csv(result);
    else
        print_result(result);

    return 0;
}
