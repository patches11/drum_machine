#ifndef SCHEDULER_H
#define SCHEDULER_H

// Scheduler — the lookahead timing core, per scheduler_spec.md (M6).
//
// Swing, per-voice push/pull, per-step nudge and humanize all flow through
// here. Steps are scheduled AHEAD of time into an event queue (a pushed
// voice must fire BEFORE its grid time — you cannot fire in the past),
// then each event fires when its offset time arrives.
//
//   * one monotonic µs timeline via ClockSource; all comparisons wrap-safe
//   * audio rides the OFFSET timeline; the LED playhead rides the GRID
//     timeline (spec §8) — groove must not make the lights jitter
//   * humanize is a bounded per-voice random walk (correlated drift reads
//     as human; per-hit white noise does not)
//   * pattern changes requested mid-bar apply at the next bar boundary

#include <stdint.h>
#include "Config.h"
#include "AppState.h"
#include "Clock.h"

// Worst-case earliest fire = |push| + |nudge| + humanize (UI clamps keep the
// real max at ~55 ms; spec constant kept for headroom).
#define MAX_AHEAD_US    90000UL
#define LOOP_MARGIN_US   5000UL   // slack for control-loop jitter
#define MAX_QUEUE          32     // 4 voices x ~2 steps in window + headroom

class SchedulerClass {
public:
  void begin(const ClockSource* clk);

  void start();
  void stop();
  bool isRunning() const { return running; }

  // Call every loop(); non-blocking. Returns true when the visual grid
  // step advanced (callers mark the LEDs dirty).
  bool tick();

  // Grid-timeline playhead for the LEDs (spec §8).
  uint8_t currentGridStep() const { return curGridStep; }

  // µs until the next thing that must happen on time (earliest queued
  // event or next grid step). Drives the OLED redraw gate.
  uint32_t timeToNextFireUs() const;

  // Queue a pattern switch; applied when the step index wraps to 0
  // (bar boundary, spec §7). 255 = nothing queued (UI shows "P2>P3").
  void requestPatternChange(uint8_t pat) { pendingPattern = pat; }
  uint8_t pendingPatternSlot() const { return pendingPattern; }

private:
  struct Event {
    uint32_t fireUs;
    uint8_t  voice, velocity;
    int8_t   pitch;
    bool     used;
  };

  uint32_t lookaheadUs() const;
  void     scheduleStep(uint8_t stepIndex, uint32_t gridUs);
  void     enqueue(uint32_t fireUs, uint8_t v, uint8_t vel, int8_t pitch);
  void     applyPendingPatternChange();
  bool     isSilencedBySolo(uint8_t voice) const;

  static bool    isSwungStep(uint8_t i, uint8_t subdiv);
  static int32_t swingOffsetUs(uint32_t stepUs, uint8_t swingPct, uint8_t subdiv);
  int32_t        humanizeOffsetUs(uint8_t v);

  const ClockSource* clock = nullptr;
  bool     running         = false;
  uint32_t stepUs          = 0;     // cached interval for this tick
  double   nextStepTimeUs  = 0;     // SCHEDULING timeline (runs ahead)
  uint8_t  nextStepIndex   = 0;     // next step to schedule (not fire)
  double   visualNextUs    = 0;     // GRID timeline (real time, drives LEDs)
  uint8_t  visualIndex     = 0;
  uint8_t  curGridStep     = 0;
  uint8_t  pendingPattern  = 255;   // 255 = none
  Event    queue[MAX_QUEUE];
  int32_t  humanizeWalk[NUM_VOICES] = {0};
  uint8_t  lookaheadStepCushion = 2;
};

extern SchedulerClass Scheduler;

#endif // SCHEDULER_H
