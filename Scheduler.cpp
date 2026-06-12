#include <Arduino.h>
#include "Scheduler.h"
#include "AudioEngine.h"

SchedulerClass Scheduler;

void SchedulerClass::begin(const ClockSource* clk) {
  clock = clk;
}

// ---------------------------------------------------------------------------
// Transport (spec §7)

void SchedulerClass::start() {
  stepUs = clock->stepIntervalUs();
  uint32_t now = clock->nowUs();
  nextStepTimeUs = (double)now;          // first step schedules immediately
  visualNextUs   = (double)now;
  nextStepIndex  = 0;
  visualIndex    = 0;
  curGridStep    = 0;
  for (auto& e : queue) e.used = false;
  for (auto& w : humanizeWalk) w = 0;
  running = true;
}

void SchedulerClass::stop() {
  running = false;
  for (auto& e : queue) e.used = false;  // flush pending — no ghost hits
}

// ---------------------------------------------------------------------------
// Lookahead window (spec §3): always covers both the step cushion and the
// largest possible "ahead" offset + loop jitter.

uint32_t SchedulerClass::lookaheadUs() const {
  uint32_t bySteps  = lookaheadStepCushion * stepUs;
  uint32_t byOffset = MAX_AHEAD_US + LOOP_MARGIN_US;
  return bySteps > byOffset ? bySteps : byOffset;
}

// ---------------------------------------------------------------------------
// Main tick (spec §4) — non-blocking, called every loop()

bool SchedulerClass::tick() {
  if (!running) return false;
  uint32_t now = clock->nowUs();

  // live tempo: future steps space at the new interval; a small one-step
  // discontinuity on change is acceptable (spec §7)
  stepUs = clock->stepIntervalUs();

  // Defensive re-anchor: the scheduling timeline normally runs AHEAD of
  // now. If a stall dragged it behind, re-anchor instead of machine-gunning
  // catch-up steps.
  if ((int32_t)((uint32_t)nextStepTimeUs - now) < -(int32_t)stepUs) {
    nextStepTimeUs = (double)now;
    visualNextUs   = (double)now;
    visualIndex    = nextStepIndex;
  }

  // (1) Schedule every step whose grid time enters the lookahead window.
  uint32_t horizon = now + lookaheadUs();
  while ((int32_t)((uint32_t)nextStepTimeUs - horizon) <= 0) {
    if (nextStepIndex == 0) applyPendingPatternChange();   // bar boundary
    scheduleStep(nextStepIndex, (uint32_t)nextStepTimeUs);
    nextStepIndex   = (nextStepIndex + 1) % pattern.length;
    nextStepTimeUs += (double)stepUs;                      // double: no drift
  }

  // (2) Advance the visual grid step (grid timeline, not offsets — §8).
  bool advanced = false;
  while ((int32_t)((uint32_t)visualNextUs - now) <= 0) {
    curGridStep   = visualIndex;
    visualIndex   = (visualIndex + 1) % pattern.length;
    visualNextUs += (double)stepUs;
    advanced = true;
  }

  // (3) Fire queued events that are due.
  for (auto& e : queue) {
    if (e.used && (int32_t)(e.fireUs - now) <= 0) {
      AudioEngine.trigger(e.voice, e.velocity, e.pitch);
      e.used = false;
    }
  }

  return advanced;
}

// ---------------------------------------------------------------------------
// Scheduling one step — all the groove math (spec §5)

void SchedulerClass::scheduleStep(uint8_t stepIndex, uint32_t gridUs) {
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    const Step& st = pattern.voices[v].steps[stepIndex];
    if (!st.active)          continue;
    if (isSilencedBySolo(v)) continue;

    // ---- timing offset (signed µs) ----
    int32_t offUs = 0;
    if (isSwungStep(stepIndex, globalState.swingSubdiv))
      offUs += swingOffsetUs(stepUs, globalState.swing, globalState.swingSubdiv);
    offUs += (int32_t)pattern.voices[v].pushPull * 1000;   // ms -> µs
    offUs += (int32_t)st.nudge * 1000;
    offUs += humanizeOffsetUs(v);

    uint32_t fireUs = gridUs + (uint32_t)offUs;            // wrap-safe

    // Defensive: offsets pushed it just behind 'now' -> fire ASAP, not drop.
    uint32_t now = clock->nowUs();
    if ((int32_t)(fireUs - now) < 0) fireUs = now;

    // ---- velocity (accent) ----
    int vel = st.velocity;
    if (st.accent) vel += globalState.accentBoost;
    if (vel > 127) vel = 127;

    enqueue(fireUs, v, (uint8_t)vel, st.pitch);
  }
}

void SchedulerClass::enqueue(uint32_t fireUs, uint8_t v, uint8_t vel,
                             int8_t pitch) {
  for (auto& e : queue) {
    if (!e.used) {
      e = { fireUs, v, vel, pitch, true };
      return;
    }
  }
  // Queue full = lookahead sizing bug. Fire now rather than drop the hit.
  AudioEngine.trigger(v, vel, pitch);
}

void SchedulerClass::applyPendingPatternChange() {
  if (pendingPattern == 255) return;
  globalState.currentPattern = pendingPattern;   // slots arrive at M10
  pendingPattern = 255;
}

bool SchedulerClass::isSilencedBySolo(uint8_t voice) const {
  const Voice& vc = pattern.voices[voice];
  if (vc.mute) return true;
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    if (pattern.voices[v].solo) return !vc.solo;  // someone solos: only solos
  }
  return false;
}

// ---------------------------------------------------------------------------
// Helper math (spec §6)

bool SchedulerClass::isSwungStep(uint8_t i, uint8_t subdiv) {
  if (subdiv == 16) return (i & 1) == 1;          // off-16ths: 1,3,5,...
  else              return (i & 3) == 2;          // off-8ths : 2,6,10,14
}

int32_t SchedulerClass::swingOffsetUs(uint32_t stepUs, uint8_t swingPct,
                                      uint8_t subdiv) {
  double f    = swingPct / 100.0;                 // 0.50..0.75
  double base = (2.0 * f - 1.0) * stepUs;         // 16th-swing delay
  return (int32_t)((subdiv == 8) ? 2.0 * base : base);
}

// Bounded per-voice random walk — correlated drift, not white noise.
int32_t SchedulerClass::humanizeOffsetUs(uint8_t v) {
  if (globalState.humanize == 0) return 0;
  int32_t maxUs  = (int32_t)globalState.humanize * 1000;
  int32_t stepUp = maxUs / 4;                     // small per-call increment
  humanizeWalk[v] += random(-stepUp, stepUp + 1);
  if (humanizeWalk[v] >  maxUs) humanizeWalk[v] =  maxUs;
  if (humanizeWalk[v] < -maxUs) humanizeWalk[v] = -maxUs;
  return humanizeWalk[v];
}

// ---------------------------------------------------------------------------

uint32_t SchedulerClass::timeToNextFireUs() const {
  if (!running) return 0xFFFFFFFFul;
  uint32_t now  = clock->nowUs();
  uint32_t best = 0xFFFFFFFFul;

  for (const auto& e : queue) {
    if (!e.used) continue;
    int32_t dt = (int32_t)(e.fireUs - now);
    uint32_t d = dt > 0 ? (uint32_t)dt : 0;
    if (d < best) best = d;
  }
  int32_t dg = (int32_t)((uint32_t)visualNextUs - now);
  uint32_t g = dg > 0 ? (uint32_t)dg : 0;
  if (g < best) best = g;

  return best;
}
