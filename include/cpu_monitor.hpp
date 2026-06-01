#pragma once

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

/// Snapshot of /proc/stat for one CPU (aggregate of all cores).
struct CpuSnapshot {
    uint64_t user    = 0;
    uint64_t nice    = 0;
    uint64_t system  = 0;
    uint64_t idle    = 0;
    uint64_t iowait  = 0;
    uint64_t irq     = 0;
    uint64_t softirq = 0;
    uint64_t steal   = 0;

    uint64_t total() const
    {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }

    uint64_t busy() const
    {
        return user + nice + system + irq + softirq + steal;
    }
};

/// Read the "cpu" aggregate line from /proc/stat.
inline CpuSnapshot read_cpu_snapshot()
{
    std::ifstream f("/proc/stat");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("cpu ", 0) == 0) {
            std::istringstream ss(line);
            std::string label;
            CpuSnapshot s;
            ss >> label >> s.user >> s.nice >> s.system >> s.idle
               >> s.iowait >> s.irq >> s.softirq >> s.steal;
            return s;
        }
    }
    return {};
}

/// Compute utilization [0..100] between two snapshots.
inline double compute_cpu_util(const CpuSnapshot& before, const CpuSnapshot& after)
{
    uint64_t total_delta = after.total() - before.total();
    uint64_t busy_delta  = after.busy()  - before.busy();
    if (total_delta == 0) return 0.0;
    return 100.0 * static_cast<double>(busy_delta) / static_cast<double>(total_delta);
}

/// Per-process CPU time from /proc/self/stat (utime + stime in clock ticks).
inline uint64_t read_process_cpu_ticks()
{
    std::ifstream f("/proc/self/stat");
    std::string token;
    // Fields: pid comm state ppid pgroup session tty_nr ... utime(14) stime(15)
    for (int i = 1; i <= 13; ++i) f >> token;
    uint64_t utime = 0, stime = 0;
    f >> utime >> stime;
    return utime + stime;
}
