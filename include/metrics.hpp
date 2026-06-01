#pragma once

#include <cstdint>
#include <optional>
#include <print>
#include <string>

// ── Metric 1: Throughput ─────────────────────────────────────────────────────
struct ThroughputResult {
    double   gbps;              // T = (ΔBytes × 8) / (Δt × 10⁹)
    uint64_t total_bytes;
    double   duration_sec;
    uint64_t syscalls;          // send() / recv() call count
    double   syscalls_per_sec;  // S = syscalls / Δt
    double   mem_bw_gbps;       // TCP: ≈ 2×T/8 (two copies); RDMA: ≈ T/8
};

// ── Metric 2: Latency (RTT/2 via ping-pong) ─────────────────────────────────
struct LatencyResult {
    double   p50_us;
    double   p99_us;
    double   p999_us;
    double   mean_us;
    double   min_us;
    double   max_us;
    uint32_t samples;
};

// ── Metric 3: CPU utilisation ────────────────────────────────────────────────
struct CpuResult {
    double cpu_pct;   // % of all cores (system-wide)
};

// ── Combined result record ────────────────────────────────────────────────────
struct BenchmarkResult {
    std::string mode;          // "tcp-blocking" | "tcp-zerocopy"
    std::string side;          // "client" | "server"
    std::size_t buffer_size;
    uint32_t    num_buffers;

    std::optional<ThroughputResult> throughput;
    std::optional<LatencyResult>    latency;
    std::optional<CpuResult>        cpu;
};

// ── Pretty-print ──────────────────────────────────────────────────────────────
inline void print_result(const BenchmarkResult& r)
{
    std::println("┌─────────────────────────────────────────────────────┐");
    std::println("│  TCP/IP Benchmark Result — {} ({})", r.mode, r.side);
    std::println("│  buffer_size = {} KiB  |  num_buffers = {}",
                 r.buffer_size / 1024, r.num_buffers);
    std::println("├─────────────────────────────────────────────────────┤");

    if (r.throughput) {
        const auto& t = *r.throughput;
        std::println("│  Metric 1 — Throughput (T)");
        std::println("│    T         = {:.2f} Gbit/s", t.gbps);
        std::println("│    bytes     = {} MB", t.total_bytes / (1024 * 1024));
        std::println("│    duration  = {:.2f} s", t.duration_sec);
        std::println("│  Metric 4 — Syscalls (S)");
        std::println("│    syscalls  = {}", t.syscalls);
        std::println("│    S         = {:.0f} calls/s", t.syscalls_per_sec);
        std::println("│  Metric 5 — Memory Bandwidth (M)");
        std::println("│    M         = {:.2f} GB/s  (≈ 2×T/8, two copies)", t.mem_bw_gbps);
    }

    if (r.latency) {
        const auto& l = *r.latency;
        std::println("│  Metric 2 — Latency RTT (L = RTT / 2)");
        std::println("│    p50       = {:.1f} µs", l.p50_us);
        std::println("│    p99       = {:.1f} µs", l.p99_us);
        std::println("│    p99.9     = {:.1f} µs", l.p999_us);
        std::println("│    mean      = {:.1f} µs  (samples={})", l.mean_us, l.samples);
    }

    if (r.cpu) {
        std::println("│  Metric 3 — CPU Utilisation (U)");
        std::println("│    U         = {:.1f} %  (system-wide)", r.cpu->cpu_pct);
    }

    std::println("└─────────────────────────────────────────────────────┘");
}

// ── CSV output for scripted comparison with Inflare results ──────────────────
inline void print_result_csv(const BenchmarkResult& r)
{
    // Header is printed once by the caller if needed
    // Format: mode,side,buf_kb,num_buf,T_gbps,S_calls_s,M_gbps,p50_us,p99_us,cpu_pct
    double T = r.throughput ? r.throughput->gbps           : 0.0;
    double S = r.throughput ? r.throughput->syscalls_per_sec: 0.0;
    double M = r.throughput ? r.throughput->mem_bw_gbps    : 0.0;
    double p50  = r.latency ? r.latency->p50_us  : 0.0;
    double p99  = r.latency ? r.latency->p99_us  : 0.0;
    double cpu  = r.cpu     ? r.cpu->cpu_pct     : 0.0;

    std::println("{},{},{},{},{:.3f},{:.0f},{:.3f},{:.1f},{:.1f},{:.1f}",
                 r.mode, r.side, r.buffer_size / 1024, r.num_buffers,
                 T, S, M, p50, p99, cpu);
}

inline void print_csv_header()
{
    std::println("mode,side,buf_kb,num_buf,T_gbps,S_calls_s,M_gbps,p50_us,p99_us,cpu_pct");
}
