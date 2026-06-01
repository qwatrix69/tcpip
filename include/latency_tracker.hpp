#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

/// Collects latency samples (in microseconds) and computes percentiles.
class LatencyTracker {
public:
    void record(double us) { samples_.push_back(us); }

    void reserve(std::size_t n) { samples_.reserve(n); }

    std::size_t count() const { return samples_.size(); }

    double percentile(double p)
    {
        if (samples_.empty()) return 0.0;
        if (!sorted_) {
            std::sort(samples_.begin(), samples_.end());
            sorted_ = true;
        }
        std::size_t idx = static_cast<std::size_t>(p / 100.0 * static_cast<double>(samples_.size() - 1));
        return samples_[idx];
    }

    double p50()  { return percentile(50.0); }
    double p99()  { return percentile(99.0); }
    double p999() { return percentile(99.9); }
    double min()  { return samples_.empty() ? 0.0 : *std::min_element(samples_.begin(), samples_.end()); }
    double max()  { return samples_.empty() ? 0.0 : *std::max_element(samples_.begin(), samples_.end()); }

    double mean() const
    {
        if (samples_.empty()) return 0.0;
        double sum = 0.0;
        for (auto v : samples_) sum += v;
        return sum / static_cast<double>(samples_.size());
    }

private:
    std::vector<double> samples_;
    bool sorted_ = false;
};
