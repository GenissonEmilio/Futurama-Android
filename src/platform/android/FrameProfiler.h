#pragma once
#include <android/log.h>
#include <sys/system_properties.h>
#include <time.h>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <vector>

namespace fut::platform {
// Opt-in developer telemetry. No per-frame allocation or clock reads when disabled.
class FrameProfiler {
public:
    FrameProfiler() {
        char value[PROP_VALUE_MAX]{};
        __system_property_get("debug.futurama.profile", value);
        enabled_ = value[0] == '1';
        if (enabled_) samples_.reserve(2048);
    }
    void Begin() {
        if (!enabled_) return;
        wall_ = Clock::now();
        cpu_ = CpuMilliseconds();
    }
    void End(const char* context, const char* level, unsigned draws) {
        if (!enabled_) return;
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - wall_).count();
        samples_.push_back(ms);
        cpuSum_ += CpuMilliseconds() - cpu_;
        draws_ += draws;
        if (samples_.size() < 300) return;
        const double seconds = std::chrono::duration<double>(Clock::now() - period_).count();
        const double sum = std::accumulate(samples_.begin(), samples_.end(), 0.0);
        std::sort(samples_.begin(), samples_.end());
        __android_log_print(ANDROID_LOG_INFO, "FuturamaPerf",
            "context=%s level=%s frames=%zu fps=%.2f frame_ms=%.3f p95_ms=%.3f max_ms=%.3f game_cpu_ms=%.3f draws=%.1f",
            context, level, samples_.size(), samples_.size() / seconds, sum / samples_.size(),
            samples_[samples_.size() * 95 / 100], samples_.back(), cpuSum_ / samples_.size(),
            double(draws_) / samples_.size());
        Reset();
    }
    void Reset() {
        samples_.clear(); cpuSum_ = 0; draws_ = 0; period_ = Clock::now();
    }
    bool Enabled() const { return enabled_; }
private:
    using Clock = std::chrono::steady_clock;
    static double CpuMilliseconds() {
        timespec t{}; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
        return double(t.tv_sec) * 1000.0 + double(t.tv_nsec) / 1000000.0;
    }
    bool enabled_ = false;
    Clock::time_point period_ = Clock::now(), wall_{};
    double cpu_ = 0, cpuSum_ = 0;
    unsigned long long draws_ = 0;
    std::vector<double> samples_;
};
}
