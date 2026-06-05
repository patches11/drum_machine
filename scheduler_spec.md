# Lookahead Scheduler — Detailed Spec (Phase 6)

This is the timing core. Swing, per-voice push/pull, per-step nudge, and humanize all flow through it. Implement against the data structures in the main plan (`Step`, `Voice`, `Pattern`, `GlobalState`).

---

## 1. The problem it solves

A naive sequencer fires every active voice the instant the playhead reaches a step. Groove breaks that: a note may need to fire **before** its grid time (a voice pushed "ahead," or a negative nudge). You cannot fire in the past. So the scheduler must:

1. Compute upcoming step grid-times in advance.
2. For each active step, compute its **actual** fire time = grid time + (swing + push/pull + nudge + humanize).
3. Queue those events and fire each when its time arrives.

The **lookahead window** must be larger than the biggest possible "ahead" (negative) offset, so even the earliest-firing note is queued before it's due.

Keep the **audio** on the offset timeline but the **visual playhead** on the grid (see §8).

---

## 2. Time base

Work in **microseconds** on one monotonic clock, behind a single `now_us()` accessor:

```cpp
inline uint32_t now_us() { return micros(); }   // 32-bit; wraps ~71 min — handle with subtraction
```

- All comparisons use **unsigned subtraction** (`(int32_t)(a - b) <= 0`) so they survive the `micros()` wrap.
- **Onset granularity:** audio only starts samples on 128-sample block boundaries (~2.9 ms at 44.1 kHz), so onsets quantize to that unless you pass a sub-block sample offset into the player (optional refinement, §9). ~2.9 ms is usually acceptable for drums.
- For tighter sync you can swap `now_us()` for an audio-sample counter (increment by `AUDIO_BLOCK_SAMPLES` each `update()`), converting samples↔µs. The rest of the logic is unchanged.

---

## 3. State & constants

```cpp
// Editable ranges (clamp in the UI)
//   Voice.pushPull : ms, ~ -50..+50   (negative = ahead/push)
//   Step.nudge     : ms, ~ -30..+30
//   GlobalState.humanize : ms, 0..~10 (max magnitude)

#define MAX_AHEAD_US     90000UL   // worst-case earliest fire = |minPushPull| + |minNudge| + humanize
#define LOOP_MARGIN_US    5000UL   // slack for control-loop jitter / a slow redraw
#define MAX_QUEUE           32     // >= NUM_VOICES * (steps in window) + headroom

struct Event { uint32_t fireUs; uint8_t voice, velocity; int8_t pitch; bool used; };

struct Scheduler {
    bool      running        = false;
    double    nextStepTimeUs = 0;   // double: avoids drift from repeated += (see §9)
    uint32_t  stepIntervalUs = 0;
    uint8_t   nextStepIndex  = 0;    // step about to be SCHEDULED (not fired)
    uint8_t   currentGridStep = 0;   // most recent grid step that has passed -> drives LEDs
    Event     queue[MAX_QUEUE];
    int32_t   humanizeWalk[NUM_VOICES] = {0};  // correlated drift, not white noise
    uint8_t   lookaheadStepCushion = 2;        // schedule at least this many steps ahead
};
```

`LOOKAHEAD_US` is derived so it always covers both the negative offsets and the loop jitter:

```cpp
uint32_t lookaheadUs(const Scheduler& s) {
    uint32_t bySteps = s.lookaheadStepCushion * s.stepIntervalUs;
    uint32_t byOffset = MAX_AHEAD_US + LOOP_MARGIN_US;
    return bySteps > byOffset ? bySteps : byOffset;
}
```

---

## 4. Main tick (call every `loop()` iteration — non-blocking)

`loop()` runs far faster than the step rate, so polling here is plenty. Nothing in here blocks.

```cpp
void schedulerTick(Scheduler& s, Pattern& pat, GlobalState& g) {
    if (!s.running) return;
    uint32_t now = now_us();

    // (1) Schedule every step whose grid time enters the lookahead window.
    uint32_t horizon = now + lookaheadUs(s);
    while ((int32_t)((uint32_t)s.nextStepTimeUs - horizon) <= 0) {
        // bar-boundary work happens when the index wraps (pattern change, §7)
        if (s.nextStepIndex == 0) applyPendingPatternChange(s, pat, g);

        scheduleStep(s, pat, g, s.nextStepIndex, (uint32_t)s.nextStepTimeUs);

        s.nextStepIndex   = (s.nextStepIndex + 1) % pat.length;
        s.nextStepTimeUs += s.stepIntervalUs;
    }

    // (2) Advance the visual grid step for the LEDs (grid timeline, not offset).
    updateCurrentGridStep(s, now);

    // (3) Fire any queued events that are due.
    for (int i = 0; i < MAX_QUEUE; i++) {
        Event& e = s.queue[i];
        if (e.used && (int32_t)(e.fireUs - now) <= 0) {
            audioEngine.trigger(e.voice, e.velocity, e.pitch);  // ratio/tune computed inside trigger
            e.used = false;
        }
    }
}
```

---

## 5. Scheduling one step (all the groove math lives here)

```cpp
void scheduleStep(Scheduler& s, Pattern& pat, GlobalState& g,
                  uint8_t stepIndex, uint32_t gridUs) {
    for (uint8_t v = 0; v < NUM_VOICES; v++) {
        Step& st = pat.voices[v].steps[stepIndex];
        if (!st.active)            continue;
        if (isSilencedBySolo(pat, v)) continue;   // mute/solo logic

        // ---- timing offset (signed microseconds) ----
        int32_t offUs = 0;
        if (isSwungStep(stepIndex, g.swingSubdiv))
            offUs += swingOffsetUs(s.stepIntervalUs, g.swing, g.swingSubdiv);
        offUs += (int32_t)pat.voices[v].pushPull * 1000;   // ms -> us
        offUs += (int32_t)st.nudge * 1000;                 // ms -> us
        offUs += humanizeOffsetUs(s, g, v);

        uint32_t fireUs = gridUs + offUs;                  // wrap-safe (unsigned add of signed)

        // Defensive: if offsets pushed it just behind 'now', fire ASAP rather than drop.
        uint32_t now = now_us();
        if ((int32_t)(fireUs - now) < 0) fireUs = now;

        // ---- velocity (accent) ----
        int vel = st.velocity;
        if (st.accent) vel += g.accentBoost;
        if (vel > 127) vel = 127;

        enqueue(s, fireUs, v, (uint8_t)vel, st.pitch);
    }
}

void enqueue(Scheduler& s, uint32_t fireUs, uint8_t v, uint8_t vel, int8_t pitch) {
    for (int i = 0; i < MAX_QUEUE; i++)
        if (!s.queue[i].used) { s.queue[i] = {fireUs, v, vel, pitch, true}; return; }
    // queue full -> drop or fire immediately; should not happen if MAX_QUEUE sized right.
}
```

---

## 6. Helper math

### Step interval
4 sixteenth-steps per quarter note:
```cpp
uint32_t stepIntervalFromBpm(uint16_t bpm) {
    return (uint32_t)(60000000.0 / bpm / 4.0);   // µs per 16th step
}
```

### Swing
Derived from pair geometry. `f = swing/100` (0.50 = straight … 0.75 = heavy). For **16th swing**, each pair is 2 steps and the off-16th is delayed by `(2f − 1) × stepInterval`. **8th swing** delays by twice that (its pair is 4 steps).

```cpp
// which steps get delayed
bool isSwungStep(uint8_t i, uint8_t subdiv) {
    if (subdiv == 16) return (i & 1) == 1;          // off-16ths: 1,3,5,...
    else              return (i & 3) == 2;          // off-8ths : 2,6,10,14
}

int32_t swingOffsetUs(uint32_t stepIntervalUs, uint8_t swingPct, uint8_t subdiv) {
    double f = swingPct / 100.0;                    // 0.50..0.75
    double base = (2.0 * f - 1.0) * stepIntervalUs; // 16th-swing delay
    return (int32_t)((subdiv == 8) ? 2.0 * base : base);
}
```
Sanity check (120 BPM, step = 125 ms, 66.7% 16th swing): `(2·0.667−1)·125 ≈ 41.7 ms` delay on odd steps — a triplet feel. ✔

### Humanize (correlated drift, not white noise)
Per-voice bounded **random walk** — small, correlated wandering, which reads as human; per-hit white noise does not. Optionally add a fixed per-voice bias for a consistent "always slightly late" tendency.

```cpp
int32_t humanizeOffsetUs(Scheduler& s, GlobalState& g, uint8_t v) {
    if (g.humanize == 0) return 0;
    int32_t maxUs  = (int32_t)g.humanize * 1000;
    int32_t stepUs = maxUs / 4;                     // small per-call increment
    s.humanizeWalk[v] += random(-stepUs, stepUs + 1);
    if (s.humanizeWalk[v] >  maxUs) s.humanizeWalk[v] =  maxUs;
    if (s.humanizeWalk[v] < -maxUs) s.humanizeWalk[v] = -maxUs;
    return s.humanizeWalk[v];                        // + optional per-voice bias
}
```

---

## 7. Transport, pattern change, tempo

```cpp
void schedulerStart(Scheduler& s, GlobalState& g) {
    s.stepIntervalUs = stepIntervalFromBpm(g.bpm);
    s.nextStepTimeUs = (double)now_us();   // first step fires ~immediately
    s.nextStepIndex  = 0;
    s.currentGridStep = 0;
    for (auto& e : s.queue) e.used = false;
    for (auto& w : s.humanizeWalk) w = 0;
    s.running = true;
}

void schedulerStop(Scheduler& s) {
    s.running = false;
    for (auto& e : s.queue) e.used = false;  // flush pending
}
```

- **Pattern change:** set a `pendingPattern` from the UI; `applyPendingPatternChange()` (called in §4 only when `nextStepIndex == 0`) swaps it so changes land on the bar boundary, not mid-bar.
- **Tempo change:** recompute `stepIntervalUs`. To avoid a phase glitch, re-anchor: keep the current phase within the step and recompute `nextStepTimeUs` from it, rather than letting the old accumulator carry stale spacing. A small one-step discontinuity is acceptable if you keep it simple.

---

## 8. Visual playhead (keep it on the grid)

Because events fire at offsets, don't drive the LED playhead off individual triggers — it would jitter. Track the grid:

```cpp
void updateCurrentGridStep(Scheduler& s, uint32_t now) {
    // currentGridStep = the latest step whose GRID time has passed 'now'.
    // Derive from nextStepIndex and nextStepTimeUs (the next step not yet reached):
    uint8_t justPassed = (s.nextStepIndex + s.pattern_length - 1) % s.pattern_length;
    // refine by comparing now against the grid time of the next step if needed
    s.currentGridStep = justPassed;
}
```
The LED renderer lights `currentGridStep` as the playhead column. Audio sits ahead/behind it by the groove offsets — which is exactly the point.

---

## 9. Precision & robustness notes

- **Drift:** repeatedly doing `nextStepTimeUs += stepIntervalUs` in pure integers accumulates rounding error over a long session. Keep `nextStepTimeUs` as `double` (above) **or** anchor each grid time to `startUs + stepCounter * stepIntervalUs` in 64-bit. Re-anchor on tempo change.
- **micros() wrap:** every time comparison uses signed-difference (`(int32_t)(a - b)`), never `a < b`, so the ~71-minute wrap is harmless.
- **Queue sizing:** with 4 voices and ~2 steps in the window, peak pending ≈ 8; `MAX_QUEUE = 32` is safe. If it ever fills, that's a bug in lookahead sizing.
- **Sub-block accuracy (optional):** to beat the ~2.9 ms block quantization, compute the sample offset of `fireUs` within the next audio block and have the resampling player skip that many input samples on start.
- **Don't block the loop:** a long OLED redraw eats into `LOOP_MARGIN_US`. Keep redraws short/dirty-gated; the margin is a safety net, not a budget to spend.

---

## 10. Test plan for this phase

1. **Straight clock:** swing 50%, all offsets 0 → all voices land exactly on the grid; metronome against a DAW confirms steady BPM with no drift over several minutes.
2. **Swing sweep:** raise swing 50→66.7→75% → odd steps progressively delayed by the predicted µs; verify against the formula.
3. **Push/pull:** set one voice to +25 ms and another to −25 ms → audibly behind / ahead while the LED playhead stays on the grid.
4. **Nudge:** per-step ±values shift only that hit.
5. **Negative-offset safety:** max push + max negative nudge on the same step → still fires on time (never dropped, never late); confirm `LOOKAHEAD_US` covers it.
6. **Tempo change while running:** no hang, no runaway, minimal glitch.
7. **Pattern change:** swaps cleanly on the next bar, not mid-bar.
8. **Stress:** all 4 voices on every step at high BPM → queue never overflows, CPU headroom intact.
