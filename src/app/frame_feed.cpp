// Frame-feed exporter for the steamos-intel-handheld game-power daemon.
// See frame_feed.h and docs/game-power-v10-framework-plan.md (contract 1.1).
//
// Design notes:
//   * Zero cost when MANGOAPP_FRAME_FEED != "1": the first statement is a
//     cached state check.
//   * Single-threaded: only mangoapp's msgrcv loop calls
//     frame_feed_record_frame(), so the rolling window needs no locking.
//   * No timer thread: the 2 Hz cadence is driven off the frame callbacks
//     themselves (write on the first frame >= 500 ms after the last write).
//   * Fail-closed: any I/O error logs once to stderr and disables the feed
//     for the rest of the session. No exception escapes into the caller.

#include "frame_feed.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// Rolling window bounds (contract 1.1: last 2.0 s or last N=256 frames,
// whichever is smaller).
constexpr double kWindowSeconds = 2.0;
constexpr size_t kMaxFrames = 256;

// Minimum spacing between file writes (~2 Hz).
constexpr double kWriteIntervalSeconds = 0.5;

// A frame that exceeds this multiple of the window median counts as a spike.
constexpr double kSpikeRatio = 1.5;

// Guard against absurd frame times poisoning the window (matches the spirit
// of the overlay's own >100 s guard; here 10 s in ms).
constexpr float kMaxSaneFrameMs = 10000.0f;

enum class FeedState { kUninitialized, kEnabled, kDisabled };

struct Sample {
    double t_s;    // CLOCK_MONOTONIC timestamp of the frame
    float ms;      // visible frame time in milliseconds
};

FeedState g_state = FeedState::kUninitialized;
std::string g_path;      // final target path
std::string g_tmp_path;  // temp path in the same directory
double g_last_write_s = 0.0;
std::deque<Sample> g_window;

double monotonic_seconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) +
           static_cast<double>(ts.tv_nsec) * 1e-9;
}

void disable_with_error(const char* what) {
    // Logged exactly once (state flips to kDisabled here).
    fprintf(stderr,
            "mangoapp frame-feed: disabled for this session (%s: %s)\n",
            what, strerror(errno));
    g_state = FeedState::kDisabled;
}

// Create every missing component of the directory that holds the feed file,
// each with mode 0700. Returns false only on a real error (EEXIST is fine).
bool ensure_parent_dir(const std::string& file_path) {
    std::string::size_type slash = file_path.find_last_of('/');
    if (slash == std::string::npos || slash == 0)
        return true;  // relative name or root-level file: nothing to make
    std::string dir = file_path.substr(0, slash);

    // Walk the path creating each component.
    for (std::string::size_type i = 1; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') {
            std::string component = dir.substr(0, i);
            if (mkdir(component.c_str(), 0700) != 0 && errno != EEXIST)
                return false;
        }
    }
    return true;
}

// Resolve the target path and prepare the directory. Returns false to leave
// the feed disabled. Returns silently (no log) when the env var is unset;
// logs once on a genuine setup failure.
bool feed_init() {
    const char* enable = getenv("MANGOAPP_FRAME_FEED");
    if (!enable || strcmp(enable, "1") != 0)
        return false;  // feature off: stay silent

    const char* override_path = getenv("MANGOAPP_FRAME_FEED_FILE");
    if (override_path && override_path[0] != '\0') {
        g_path = override_path;
    } else {
        const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
        if (!runtime_dir || runtime_dir[0] == '\0') {
            fprintf(stderr,
                    "mangoapp frame-feed: disabled (MANGOAPP_FRAME_FEED=1 but "
                    "XDG_RUNTIME_DIR is unset and MANGOAPP_FRAME_FEED_FILE was "
                    "not provided)\n");
            return false;
        }
        g_path = std::string(runtime_dir) +
                 "/steamos-intel-handheld/frame-feed.json";
    }

    if (!ensure_parent_dir(g_path)) {
        disable_with_error("mkdir");
        // disable_with_error already set kDisabled; return false so the caller
        // does not flip us back to kEnabled.
        return false;
    }

    // Temp file lives in the same directory (same filesystem) so rename() is
    // atomic. Namespace it by pid to avoid collisions between processes.
    g_tmp_path = g_path + "." + std::to_string(static_cast<long>(getpid())) +
                 ".tmp";
    return true;
}

void trim_window(double now_s) {
    const double cutoff = now_s - kWindowSeconds;
    while (!g_window.empty() &&
           (g_window.front().t_s < cutoff || g_window.size() > kMaxFrames)) {
        g_window.pop_front();
    }
}

// Atomically replace g_path with `json` via temp-file + rename. Returns
// false on any I/O error (caller disables the feed).
bool write_atomic(const std::string& json) {
    int fd = open(g_tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return false;

    const char* p = json.data();
    size_t remaining = json.size();
    while (remaining > 0) {
        ssize_t w = write(fd, p, remaining);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            unlink(g_tmp_path.c_str());
            return false;
        }
        p += w;
        remaining -= static_cast<size_t>(w);
    }
    close(fd);

    if (rename(g_tmp_path.c_str(), g_path.c_str()) != 0) {
        unlink(g_tmp_path.c_str());
        return false;
    }
    return true;
}

// Compute stats over the current window and, if valid, write them. Returns
// false only on an I/O error that should disable the feed.
bool compute_and_write(double now_s, uint32_t app_pid) {
    const size_t n = g_window.size();
    if (n == 0)
        return true;  // nothing to report this interval; not an error

    std::vector<float> sorted;
    sorted.reserve(n);
    double sum_ms = 0.0;
    float worst_ms = 0.0f;
    for (const Sample& s : g_window) {
        sorted.push_back(s.ms);
        sum_ms += s.ms;
        if (s.ms > worst_ms)
            worst_ms = s.ms;
    }
    std::sort(sorted.begin(), sorted.end());

    const float last_frame_ms = g_window.back().ms;

    // p95 frame time: value at the 95th percentile (ascending, high = slow).
    size_t p95_idx = static_cast<size_t>(std::lround(0.95 * (n - 1)));
    if (p95_idx >= n)
        p95_idx = n - 1;
    const float p95_frame_ms = sorted[p95_idx];

    // Median for the spike threshold.
    const float median_ms =
        (n % 2 == 1) ? sorted[n / 2]
                     : 0.5f * (sorted[n / 2 - 1] + sorted[n / 2]);

    const double mean_ms = sum_ms / static_cast<double>(n);
    const double avg_fps = (mean_ms > 0.0) ? (1000.0 / mean_ms) : 0.0;

    // Skip this interval if the core metrics are not finite positive.
    if (!std::isfinite(avg_fps) || avg_fps <= 0.0 ||
        !std::isfinite(p95_frame_ms) || p95_frame_ms <= 0.0f)
        return true;

    // Spike count: frames exceeding kSpikeRatio * median.
    unsigned spike_count = 0;
    if (median_ms > 0.0f) {
        const float threshold = kSpikeRatio * median_ms;
        for (const Sample& s : g_window)
            if (s.ms > threshold)
                ++spike_count;
    }

    // Actual span covered by the window (target 2.0 s).
    const double window_s = g_window.back().t_s - g_window.front().t_s;

    char buf[512];
    int len = snprintf(
        buf, sizeof(buf),
        "{"
        "\"schema\":\"steamos-intel-handheld-frame-feed-v1\","
        "\"pid\":%ld,"
        "\"updated_monotonic_s\":%.3f,"
        "\"window_s\":%.3f,"
        "\"frame_count\":%zu,"
        "\"avg_fps\":%.2f,"
        "\"p95_frame_ms\":%.2f,"
        "\"last_frame_ms\":%.2f,"
        "\"spike\":{\"count\":%u,\"worst_ms\":%.2f}"
        "}\n",
        static_cast<long>(app_pid != 0 ? app_pid
                                       : static_cast<uint32_t>(getpid())),
        now_s, window_s, n, avg_fps, static_cast<double>(p95_frame_ms),
        static_cast<double>(last_frame_ms), spike_count,
        static_cast<double>(worst_ms));
    if (len < 0 || len >= static_cast<int>(sizeof(buf)))
        return true;  // formatting glitch; skip this interval, keep the feed

    return write_atomic(std::string(buf, static_cast<size_t>(len)));
}

}  // namespace

void frame_feed_record_frame(uint64_t visible_frametime_ns, uint32_t app_pid) {
    if (g_state == FeedState::kDisabled)
        return;

    try {
        if (g_state == FeedState::kUninitialized) {
            if (!feed_init()) {
                // feed_init leaves g_state as kUninitialized when the feature
                // is simply off, or kDisabled after logging a real error.
                if (g_state != FeedState::kDisabled)
                    g_state = FeedState::kDisabled;  // off: never retry
                return;
            }
            g_state = FeedState::kEnabled;
        }

        const float frame_ms = static_cast<float>(visible_frametime_ns) / 1e6f;
        if (!(frame_ms > 0.0f) || frame_ms > kMaxSaneFrameMs)
            return;  // drop non-positive / absurd frames, keep the feed live

        const double now_s = monotonic_seconds();
        g_window.push_back(Sample{now_s, frame_ms});
        trim_window(now_s);

        if (now_s - g_last_write_s < kWriteIntervalSeconds)
            return;
        g_last_write_s = now_s;

        if (!compute_and_write(now_s, app_pid))
            disable_with_error("write");
    } catch (...) {
        // Nothing may propagate into the render/message path.
        fprintf(stderr,
                "mangoapp frame-feed: disabled after unexpected exception\n");
        g_state = FeedState::kDisabled;
    }
}
