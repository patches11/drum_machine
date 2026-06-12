#ifndef CLOCK_H
#define CLOCK_H

// Clock — the ONLY place the sequencer/scheduler gets musical time from.
//
// Rule (from the implementation plan): no module calls micros()/millis()
// for MUSICAL timing except through this interface. UI debounce etc. may
// use millis() freely.
//
// Future MIDI sync = a new module providing an alternate ClockSource
// (nowUs disciplined by MIDI clock ticks, stepIntervalUs from smoothed
// tick deltas, isExternal=true) — no sequencer changes needed, because
// every downstream comparison is wrap-safe signed subtraction
// (scheduler_spec.md §2/§9).

#include <Arduino.h>
#include "AppState.h"

struct ClockSource {
  uint32_t (*nowUs)();           // monotonic µs (wraps ~71 min — callers
                                 //  must compare via (int32_t)(a - b))
  uint32_t (*stepIntervalUs)();  // µs per 16th step at current tempo
  bool     (*isExternal)();      // true when slaved to external clock
};

// µs per 16th step: 4 sixteenths per quarter (scheduler_spec.md §6)
inline uint32_t stepIntervalFromBpm(uint16_t bpm) {
  return (uint32_t)(60000000.0 / (double)bpm / 4.0);
}

// --- default internal clock -------------------------------------------------

inline uint32_t internalNowUs()        { return micros(); }
inline uint32_t internalStepInterval() { return stepIntervalFromBpm(globalState.bpm); }
inline bool     internalIsExternal()   { return false; }

static const ClockSource INTERNAL_CLOCK = {
  internalNowUs, internalStepInterval, internalIsExternal
};

#endif // CLOCK_H
