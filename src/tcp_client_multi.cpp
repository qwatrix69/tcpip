/**
 * tcp_client_multi.cpp — многоканальный TCP/IP benchmark клиент (серия Д)
 *
 * Открывает N параллельных соединений к серверу (каждое в своём потоке) и
 * непрерывно отправляет буферы. Аналог N клиентов цепочки серверов Inflare.
 *
 * Usage:
 *   ./tcp_bench_client_multi --addr 192.168.1.2 --port 5556 --channels 4 \
 *                            --buf-size 1048576 --duration 10
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

struct ChannelStats {
    std::atomic<uint64_t> bytes    = 0;
    std::atomic<uint64_t> syscalls = 0;
};

/// One sender thread: connects and streams buffers for the whole window.
static void channel_sender(const BenchConfig& cfg, uint32_t channel_idx,
                           ChannelStats& stats,
                           std::chrono::steady_clock::time_point measure_start,
                           std::chrono::steady_clock::time_point measure_end)
{
    int fd = -1;
    try {
        fd = connect_to_server(cfg.server_addr, cfg.port);
        configure_socket(fd, cfg.buffer_size);
        if (cfg.zero_copy) enable_zero_copy(fd);
    } catch (const std::exception& e) {
        std::println(stderr, "[Client] Channel {} connect failed: {}", channel_idx, e.what());
        return;
    }

    auto buf = std::make_unique<char[]>(cfg.buffer_size);
    std::memset(buf.get(), 0xAB, cfg.buffer_size);
    const int send_flags = cfg.zero_copy ? MSG_ZEROCOPY : 0;

    while (!g_stop.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();
        if (now >= measure_end) break;

        if (!send_all(fd, buf.get(), cfg.buffer_size, send_flags)) {
            std::println("[Client] Channel {} send failed.", channel_idx);
            break;
        }
        if (cfg.zero_copy) drain_zerocopy_completions(fd);

        if (now >= measure_start) {
            stats.bytes.fetch_add(cfg.buffer_size, std::memory_order_relaxed);
            stats.syscalls.fetch_add(1, std::memory_order_relaxed);
        }
    }
    ::close(fd);
}

int main(int argc, char** argv)
{
    std::println("==========================================");
    std::println("   TCP/IP Multi-Channel Benchmark — Client");
    std::println("==========================================");

    BenchConfig cfg;
    uint32_t channels = 4;

    std::vector<char*> passthrough;
    passthrough.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if ((a == "--channels" || a == "-c") && i + 1 < argc) {
            channels = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else {
            passthrough.push_back(argv[i]);
        }
    }
    try {
        cfg = parse_args(static_cast<int>(passthrough.size()), passthrough.data());
    } catch (const std::exception& e) {
        std::println(stderr, "Error: {}", e.what());
        return 1;
    }

    std::println("  channels      : {}", channels);
    print_config(cfg);

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Shared measurement window (all channels aligned)
    auto wall_start    = std::chrono::steady_clock::now();
    auto measure_start = wall_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(cfg.warmup_sec));
    auto measure_end   = measure_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(cfg.duration_sec));

    std::vector<ChannelStats> stats(channels);
    CpuSnapshot cpu_before = read_cpu_snapshot();

    std::println("[Client] Launching {} sender threads...", channels);
    std::vector<std::thread> senders;
    senders.reserve(channels);
    for (uint32_t i = 0; i < channels; ++i) {
        senders.emplace_back(channel_sender, std::cref(cfg), i,
                             std::ref(stats[i]), measure_start, measure_end);
    }
    for (auto& s : senders) s.join();

    CpuSnapshot cpu_after = read_cpu_snapshot();
    double cpu_pct = compute_cpu_util(cpu_before, cpu_after);

    double dur_sec = cfg.duration_sec;
    uint64_t total_bytes    = 0;
    uint64_t total_syscalls = 0;
    std::println("\n── Per-channel results ──");
    for (uint32_t i = 0; i < channels; ++i) {
        uint64_t b = stats[i].bytes.load();
        double ch_gbps = (static_cast<double>(b) * 8.0) / (dur_sec * 1e9);
        std::println("  channel {}: {:.2f} Gbit/s ({} MB)",
                     i, ch_gbps, b / (1024 * 1024));
        total_bytes    += b;
        total_syscalls += stats[i].syscalls.load();
    }

    double agg_gbps = (static_cast<double>(total_bytes) * 8.0) / (dur_sec * 1e9);
    double sps      = static_cast<double>(total_syscalls) / dur_sec;
    double mbw      = cfg.zero_copy ? agg_gbps / 8.0 : agg_gbps / 8.0;

    if (cfg.csv_output) {
        std::string mode = cfg.zero_copy ? "tcp-multi-zc" : "tcp-multi";
        std::println("{},client,{},{},{:.3f},{:.0f},{:.3f},0.0,0.0,{:.1f}",
                     mode, cfg.buffer_size / 1024, channels,
                     agg_gbps, sps, mbw, cpu_pct);
    } else {
        std::println("\n┌─────────────────────────────────────────────────────┐");
        std::println("│  Multi-Channel Aggregate ({} channels)", channels);
        std::println("│  buffer_size = {} KiB", cfg.buffer_size / 1024);
        std::println("├─────────────────────────────────────────────────────┤");
        std::println("│  T_aggregate = {:.2f} Gbit/s", agg_gbps);
        std::println("│  T_per_chan  = {:.2f} Gbit/s (среднее)", agg_gbps / channels);
        std::println("│  syscalls/s  = {:.0f}", sps);
        std::println("│  CPU         = {:.1f} %% ({} sender threads)", cpu_pct, channels);
        std::println("└─────────────────────────────────────────────────────┘");
    }

    return 0;
}
