/**
 * tcp_server.cpp — TCP/IP benchmark server
 *
 * Modes:
 *   default   : throughput — receive as fast as possible, report T, S, M
 *   --ping-pong: latency   — echo each buffer back, measure RTT on client side
 *
 * Usage:
 *   ./tcp_bench_server [options]
 *   ./tcp_bench_server --port 5556 --buf-size 1048576 --num-buf 64 --duration 10
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <memory>
#include <print>
#include <thread>
#include <vector>

#include "bench_config.hpp"
#include "cpu_monitor.hpp"
#include "metrics.hpp"
#include "socket_utils.hpp"

namespace
{
std::atomic_bool g_stop = false;

void signal_handler(int) { g_stop.store(true); }
}

// ── Throughput mode ───────────────────────────────────────────────────────────

static BenchmarkResult run_throughput_server(int conn_fd, const BenchConfig& cfg)
{
    auto buf = std::make_unique<char[]>(cfg.buffer_size);

    uint64_t total_bytes   = 0;
    uint64_t total_syscalls = 0;

    auto wall_start  = std::chrono::steady_clock::now();
    auto warmup_end  = wall_start + std::chrono::duration<double>(cfg.warmup_sec);
    auto measure_end = warmup_end + std::chrono::duration<double>(cfg.duration_sec);

    bool measuring = false;
    uint64_t meas_bytes    = 0;
    uint64_t meas_syscalls = 0;

    CpuSnapshot cpu_before{};
    std::chrono::steady_clock::time_point meas_start;

    std::println("[Server] Receiving (warmup={:.1f}s, measure={:.1f}s)...",
                 cfg.warmup_sec, cfg.duration_sec);

    while (!g_stop.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();

        if (!measuring && now >= warmup_end) {
            measuring    = true;
            meas_start   = now;
            cpu_before   = read_cpu_snapshot();
            meas_bytes   = 0;
            meas_syscalls= 0;
            std::println("[Server] Warmup done, measuring...");
        }

        if (measuring && now >= measure_end) break;

        // recv() one full buffer — counts as one "syscall"
        if (!recv_all(conn_fd, buf.get(), cfg.buffer_size)) {
            std::println("[Server] Connection closed by client.");
            break;
        }
        ++total_syscalls;
        total_bytes += cfg.buffer_size;

        if (measuring) {
            ++meas_syscalls;
            meas_bytes += cfg.buffer_size;
        }
    }

    CpuSnapshot cpu_after = read_cpu_snapshot();
    double cpu_pct = compute_cpu_util(cpu_before, cpu_after);

    auto meas_dur = std::chrono::steady_clock::now() - meas_start;
    double dur_sec = std::chrono::duration<double>(meas_dur).count();

    double gbps = (static_cast<double>(meas_bytes) * 8.0) / (dur_sec * 1e9);
    double sps  = static_cast<double>(meas_syscalls) / dur_sec;
    double mbw  = gbps * 2.0 / 8.0;  // TCP: kernel→user copy on recv side

    ThroughputResult thr;
    thr.gbps             = gbps;
    thr.total_bytes      = meas_bytes;
    thr.duration_sec     = dur_sec;
    thr.syscalls         = meas_syscalls;
    thr.syscalls_per_sec = sps;
    thr.mem_bw_gbps      = mbw;

    BenchmarkResult res;
    res.mode        = cfg.zero_copy ? "tcp-zerocopy" : "tcp-blocking";
    res.side        = "server";
    res.buffer_size = cfg.buffer_size;
    res.num_buffers = cfg.num_buffers;
    res.throughput  = thr;
    res.cpu         = CpuResult{cpu_pct};
    return res;
}

// ── Ping-pong (latency) mode ──────────────────────────────────────────────────

static void run_pingpong_server(int conn_fd, const BenchConfig& cfg)
{
    auto buf = std::make_unique<char[]>(cfg.buffer_size);
    std::println("[Server] Ping-pong mode: echoing {} iterations...", cfg.latency_iters);

    // Use small latency buffer (64 B) for pure latency, full buffer for bandwidth-latency
    const std::size_t ping_size = std::min(cfg.buffer_size, std::size_t{65536});

    for (uint32_t i = 0; i < cfg.latency_iters && !g_stop; ++i) {
        if (!recv_all(conn_fd, buf.get(), ping_size)) break;
        if (!send_all(conn_fd, buf.get(), ping_size)) break;
    }
    std::println("[Server] Ping-pong done.");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    std::println("==========================================");
    std::println("   TCP/IP Benchmark — Server");
    std::println("==========================================");

    BenchConfig cfg;
    try { cfg = parse_args(argc, argv); }
    catch (const std::exception& e) {
        std::println(stderr, "Error: {}", e.what());
        std::println(stderr, "Usage: {} [--port N] [--buf-size B] [--num-buf N]"
                             " [--duration S] [--warmup S] [--ping-pong] [--csv]", argv[0]);
        return 1;
    }

    print_config(cfg);

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    int server_fd = create_server_socket(cfg.port);
    std::println("[Server] Listening on port {}...", cfg.port);

    int conn_fd = accept_connection(server_fd);
    configure_socket(conn_fd, cfg.buffer_size);

    BenchmarkResult result;
    if (cfg.ping_pong) {
        run_pingpong_server(conn_fd, cfg);
        // Latency result is computed on the client side
        result.mode        = "tcp-blocking";
        result.side        = "server";
        result.buffer_size = cfg.buffer_size;
        result.num_buffers = cfg.num_buffers;
    } else {
        result = run_throughput_server(conn_fd, cfg);
    }

    ::close(conn_fd);
    ::close(server_fd);

    if (!cfg.ping_pong) {
        if (cfg.csv_output)
            print_result_csv(result);
        else
            print_result(result);
    }

    return 0;
}
