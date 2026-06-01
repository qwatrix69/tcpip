/**
 * tcp_server_multi.cpp — многоканальный TCP/IP benchmark сервер (серия Д)
 *
 * Аналог режима цепочки серверов Inflare (RdmaStreamChain): принимает N
 * параллельных клиентских соединений, каждое обслуживается отдельным потоком,
 * совокупная пропускная способность агрегируется. Позволяет оценить
 * масштабируемость TCP/IP при росте числа каналов и сравнить с линейным
 * масштабированием RDMA.
 *
 * Usage:
 *   ./tcp_bench_server_multi --port 5556 --channels 4 \
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

/// Per-channel statistics gathered by each worker thread.
struct ChannelStats {
    std::atomic<uint64_t> bytes    = 0;
    std::atomic<uint64_t> syscalls = 0;
};

/// One worker thread: receives buffers on its connection until measure window ends.
static void channel_worker(int conn_fd, const BenchConfig& cfg, uint32_t channel_idx,
                           ChannelStats& stats,
                           std::chrono::steady_clock::time_point measure_start,
                           std::chrono::steady_clock::time_point measure_end)
{
    auto buf = std::make_unique<char[]>(cfg.buffer_size);

    while (!g_stop.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();
        if (now >= measure_end) break;

        if (!recv_all(conn_fd, buf.get(), cfg.buffer_size)) {
            std::println("[Server] Channel {} closed.", channel_idx);
            break;
        }

        // Count only inside the measurement window
        if (now >= measure_start) {
            stats.bytes.fetch_add(cfg.buffer_size, std::memory_order_relaxed);
            stats.syscalls.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

int main(int argc, char** argv)
{
    std::println("==========================================");
    std::println("   TCP/IP Multi-Channel Benchmark — Server");
    std::println("==========================================");

    BenchConfig cfg;
    uint32_t channels = 4;

    // Manual parse to capture --channels in addition to shared options
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

    int server_fd = create_server_socket(cfg.port);
    std::println("[Server] Listening on port {}, waiting for {} channels...",
                 cfg.port, channels);

    // Accept all channel connections first
    std::vector<int> conn_fds;
    conn_fds.reserve(channels);
    for (uint32_t i = 0; i < channels; ++i) {
        int fd = accept_connection(server_fd);
        configure_socket(fd, cfg.buffer_size);
        conn_fds.push_back(fd);
        std::println("[Server] Channel {} connected.", i);
    }
    std::println("[Server] All {} channels connected. Starting measurement.", channels);

    // Define the shared measurement window
    auto wall_start   = std::chrono::steady_clock::now();
    auto measure_start = wall_start + std::chrono::duration<double>(cfg.warmup_sec);
    auto measure_end   = measure_start + std::chrono::duration<double>(cfg.duration_sec);

    std::vector<ChannelStats> stats(channels);
    CpuSnapshot cpu_before = read_cpu_snapshot();

    // Launch one worker per channel
    std::vector<std::thread> workers;
    workers.reserve(channels);
    for (uint32_t i = 0; i < channels; ++i) {
        workers.emplace_back(channel_worker, conn_fds[i], std::cref(cfg), i,
                             std::ref(stats[i]), measure_start, measure_end);
    }
    for (auto& w : workers) w.join();

    CpuSnapshot cpu_after = read_cpu_snapshot();
    double cpu_pct = compute_cpu_util(cpu_before, cpu_after);

    // Aggregate
    double dur_sec = cfg.duration_sec;
    uint64_t total_bytes    = 0;
    uint64_t total_syscalls = 0;
    std::println("\n── Per-channel results ──");
    for (uint32_t i = 0; i < channels; ++i) {
        uint64_t b = stats[i].bytes.load();
        uint64_t s = stats[i].syscalls.load();
        double ch_gbps = (static_cast<double>(b) * 8.0) / (dur_sec * 1e9);
        std::println("  channel {}: {:.2f} Gbit/s  ({} MB, {} recv)",
                     i, ch_gbps, b / (1024 * 1024), s);
        total_bytes    += b;
        total_syscalls += s;
    }

    double agg_gbps = (static_cast<double>(total_bytes) * 8.0) / (dur_sec * 1e9);
    double sps      = static_cast<double>(total_syscalls) / dur_sec;
    double mbw      = agg_gbps * 2.0 / 8.0;

    for (int fd : conn_fds) ::close(fd);
    ::close(server_fd);

    if (cfg.csv_output) {
        // mode,side,buf_kb,num_buf,T_gbps,S_calls_s,M_gbps,p50_us,p99_us,cpu_pct
        // num_buf column repurposed to carry channel count for series Д
        std::println("tcp-multi,{},{},{},{:.3f},{:.0f},{:.3f},0.0,0.0,{:.1f}",
                     channels, cfg.buffer_size / 1024, channels,
                     agg_gbps, sps, mbw, cpu_pct);
    } else {
        std::println("\n┌─────────────────────────────────────────────────────┐");
        std::println("│  Multi-Channel Aggregate ({} channels)", channels);
        std::println("│  buffer_size = {} KiB", cfg.buffer_size / 1024);
        std::println("├─────────────────────────────────────────────────────┤");
        std::println("│  T_aggregate = {:.2f} Gbit/s", agg_gbps);
        std::println("│  T_per_chan  = {:.2f} Gbit/s (среднее)", agg_gbps / channels);
        std::println("│  syscalls/s  = {:.0f}", sps);
        std::println("│  M           = {:.2f} GB/s", mbw);
        std::println("│  CPU         = {:.1f} %% (system-wide, {} worker threads)",
                     cpu_pct, channels);
        std::println("└─────────────────────────────────────────────────────┘");
    }

    return 0;
}
