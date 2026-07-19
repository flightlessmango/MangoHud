#pragma once

#include <cstdint>

// Frame-feed exporter for the steamos-intel-handheld game-power daemon.
//
// Entirely guarded by the env var MANGOAPP_FRAME_FEED=1 (read once and
// cached). When the feed is disabled the call is a single cached-state
// comparison and returns immediately: no allocation, no I/O, no overhead.
//
// When enabled it maintains a rolling window of the most recent frame
// times and, at ~2 Hz, atomically writes a compact JSON summary (see
// docs/game-power-v10-framework-plan.md contract 1.1) to
// $XDG_RUNTIME_DIR/steamos-intel-handheld/frame-feed.json (overridable via
// MANGOAPP_FRAME_FEED_FILE).
//
// Must be called once per visible game frame from the mangoapp message
// read path. It is single-threaded by contract (only the msgrcv loop calls
// it) and never throws into the caller.
void frame_feed_record_frame(uint64_t visible_frametime_ns, uint32_t app_pid);
