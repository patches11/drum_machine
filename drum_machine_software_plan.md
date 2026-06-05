# Teensy Drum Machine — Software Implementation Plan

A hand-off spec for building the firmware. Target platform: **Teensy 3.6** (180 MHz Cortex-M4F) + **PJRC Audio Shield (SGTL5000)**. Audio is processed by the Teensy Audio Library in 128-sample blocks at 44.1 kHz.

---

## 1. Hardware summary

| Element | Detail |
|---|---|
| MCU + codec | Teensy 3.6 + Audio Shield (SGTL5000), I²S audio, I²C control |
| Keyboard | 12 arcade buttons = one chromatic octave (C–B) |
| Voices | 4 sample-playback voices |
| Mic | SGTL5000 mic input — used for **live sampling** |
| Encoders | 4 rotary encoders, each with a push switch (modeless control) |
| LEDs | 64 × WS2812, arranged 16 × 4 (16 steps × 4 voices) |
| Display | SSD1306 128×64 OLED, I²C (shares the codec's bus) |
| Output | Mono — sum L+R → amp → speaker |
| Storage | SD card (prefer the Teensy 3.6 built-in SDIO slot) + EEPROM for globals |
| Extra buttons | Accent, Transport (play/stop), Mode, Shift, Octave-shift |

### Reserved pins (do NOT reassign)
- **I²S audio:** 9, 11, 13, 22, 23
- **I²C (codec + OLED):** 18 (SDA), 19 (SCL)
- **Audio-shield SD (if used):** 7, 10, 12, 14 — *avoid by using the Teensy 3.6 built-in SDIO slot instead, which frees these.*

> **Action for implementer:** verify all shield pins against the current PJRC Audio Shield pinout for your board revision before finalizing the pin map below.

### Suggested pin map (verify & adjust)
- WS2812 data: a **WS2812Serial-supported TX pin** (e.g., 1 or 5 on the 3.6) — confirm against the library's supported-pin list.
- 4 encoders: 8 free digital pins (all Teensy 3.6 digital pins are interrupt-capable).
- 4 encoder push + Accent/Transport/Mode/Shift/Octave: remaining free digital pins, or fold into the button matrix.
- 12-key keyboard: **4×3 matrix with one diode per key** (prevents ghosting on chords/multi-press), or direct pins if spare.

---

## 2. Software architecture

Three layers with strict real-time discipline:

1. **Audio engine** — runs in the audio interrupt (AudioStream `update()`). Never blocked, never does I/O.
2. **Sequencer / scheduler** — sample-accurate timing derived from the audio clock; emits trigger events.
3. **Control + UI loop** — `loop()` polls inputs, runs the scheduler, drives LEDs, redraws the OLED on change.

**Hard rules:**
- No `delay()` anywhere.
- No heavy work, file I/O, or blocking calls in interrupt context.
- OLED redraws and SD access happen only in the control loop, gated by dirty flags.
- LEDs use **WS2812Serial** (DMA-based, does not disable interrupts) — *not* Adafruit NeoPixel, which would glitch audio.

---

## 3. Core data structures

```cpp
#define NUM_VOICES 4
#define NUM_STEPS  16

struct Step {
    bool    active;     // hit present?
    bool    accent;     // accent flag (set if accent held on entry)
    uint8_t velocity;   // 0..127 base velocity
    int8_t  pitch;      // semitone offset (from 12-key board / octave shift)
    int8_t  nudge;      // per-step micro-timing offset (ticks/ms)
};

struct Voice {
    Step     steps[NUM_STEPS];
    uint8_t  sampleId;        // which sample is loaded
    int8_t   rootSemis;       // coarse tune (octave calibration)
    int8_t   fineCents;       // fine tune, -100..+100
    uint8_t  level;           // mix level
    uint8_t  decay;           // amp-envelope decay
    uint8_t  filterCut;       // lowpass cutoff
    int8_t   pushPull;        // per-voice timing offset, ms (groove "feel")
    uint8_t  chokeGroup;      // 0 = none; voices sharing a group cut each other
    bool     mute;
    bool     solo;
};

struct Pattern {
    Voice   voices[NUM_VOICES];
    uint8_t length;           // steps per loop (default 16)
};

struct GlobalState {
    uint16_t bpm;             // tempo
    uint8_t  swing;           // 50..75 (%)
    uint8_t  swingSubdiv;     // 8 or 16
    uint8_t  accentBoost;     // velocity added by accent
    uint8_t  humanize;        // 0..N ms of gentle, biased jitter
    uint8_t  currentPattern;
    // master FX params: comp threshold/ratio/makeup, reverb/delay sends, master filter
};
```

Patterns live in RAM during play; banks of patterns + kits are saved to SD.

---

## 4. Audio signal graph

```
mic in ──► [record buffer / SD]                (sampling path, recording only)

voice[0..3]: ResamplingPlayer ─► Envelope ─► StateVariable LP ─► (mixer ch)
                     │
                     └─ playbackRate = 2^((key - root)/12 + octShift) * 2^(cents/1200)

mixer(4) ─► master insert: [Compressor/Limiter] ─► [Master Filter] ─► out (mono)
   │
   └─ send ─► [Reverb (Freeverb)] / [Delay] ─► blended back at output
```

- **Resampling players:** stock `AudioPlayMemory` cannot pitch; use a community variable-rate player (e.g., newdigate `teensy-variable-playback`) or a custom interpolating `AudioStream`.
- **Compressor:** not in the stock library — implement as a custom `AudioStream` (envelope follower + gain calc: threshold/ratio/attack/release/makeup). A waveshaper soft-clip is the minimum viable master limiter and the anti-clipping safety net.
- **Sends (reverb/delay)** are optional/late-phase. Watch CPU (`AudioProcessorUsageMax()`) and `AudioMemory()` sizing.
- Sum to mono for the single speaker so nothing is lost.

---

## 5. Subsystems / modules

- **AudioEngine** — builds the graph, owns voice players/envelopes/filters/mixer/FX; `trigger(voice, velocity, pitchSemis)` sets gain + playback rate and fires the one-shot; handles choke groups and per-voice retrigger.
- **Sampler** — records from mic to a buffer (or SD), trims/normalizes, stores, and assigns a sample to a voice.
- **SampleStore** — loads/assigns samples (RAM/flash for snappy triggering; SD for capacity); tracks per-sample root note.
- **Sequencer** — owns the Pattern data; advances the playhead; for each due step computes the final fire time and velocity and pushes events to the scheduler.
- **Scheduler** — a small time-sorted event queue with **lookahead** (≥ max "ahead"/push offset) so notes can fire *before* the grid; applies swing + per-voice push/pull + per-step nudge + humanize. Clock derived from the audio block counter for sample accuracy.
- **Input** — encoder quadrature (Encoder lib) with acceleration; button debounce (Bounce2); 12-key matrix scan with N-key rollover; maps raw input to per-mode actions.
- **UI / Display** — `drawScreen(mode, state)` renders the 4-column label/value layout (one column per encoder, plus bars/graphics the OLED allows); dirty-flag redraw; ~1 s flash messages on mode/voice change.
- **LEDs** — renders the 16×4 grid: pattern on/off, **brightness = velocity**, playhead column, edit cursor, selected-voice row highlight, accent color; via WS2812Serial.
- **Storage** — save/load patterns, kits (sample assignments + per-voice params + tune), and songs to SD; globals to EEPROM; defines the on-disk format.
- **AppState / Modes** — mode state machine and the central state the UI and engine read from.

### Modes (UI)
Home/Mix · Pattern Edit · Sound Edit · Tempo/Swing · Feel (per-voice push/pull) · Sample/Record. Encoders are modeless; their four on-screen labels always reflect the current mode so there is never a "what does this knob do?" moment.

---

## 6. Build phases (incremental, each testable)

- **Phase 0 — Skeleton & I/O bring-up.** Project compiles; audio passthrough confirmed; OLED prints; LEDs light; encoders/buttons report over serial.
- **Phase 1 — Audio engine, fixed pitch.** 4 sample voices triggered manually → envelope → filter → mixer → output. Verify `AudioMemory`/CPU headroom.
- **Phase 2 — Sequencer core.** 16 steps, one pattern, internal clock, playhead on LEDs, voices trigger on active steps. Tempo control.
- **Phase 3 — Per-step data & editing.** Velocity, accent (button), step on/off via cursor + keys; LED feedback (brightness = velocity, playhead, cursor).
- **Phase 4 — Pitch & tuning.** Resampling player; 12-key octave; per-voice coarse/fine tune; per-step pitch; octave-shift button.
- **Phase 5 — Sound params & full UI.** Per-voice level/decay/filter on encoders; the modeless 4-column OLED screens for all modes.
- **Phase 6 — Groove.** Lookahead scheduler; swing + subdivision; per-voice push/pull; humanize.
- **Phase 7 — Sampling.** Mic record → trim/normalize → assign to a voice.
- **Phase 8 — Effects.** Master compressor/limiter (custom), master filter sweep, optional reverb/delay sends.
- **Phase 9 — Persistence.** Save/load patterns & kits to SD; globals to EEPROM.
- **Phase 10 — Performance & polish.** Multiple patterns + queued switching, song/chain mode, mute/solo, choke groups, clear/copy pattern, tap tempo.

---

## 7. Libraries / dependencies

- Teensy **Audio** library; **Wire** (I²C)
- **Encoder** (PJRC); **Bounce2**
- **WS2812Serial** (LED, DMA, interrupt-safe)
- **U8g2** or **Adafruit_SSD1306** (OLED)
- **SD / SdFat** (storage); **EEPROM** (globals)
- A variable-rate sample player (community `teensy-variable-playback`) or a custom interpolating player

---

## 8. Key risks & gotchas

- **Keyboard ghosting:** matrix without per-key diodes corrupts multi-key presses — add diodes.
- **LED power:** 64 WS2812 can draw >1 A; power them from a dedicated 5 V rail (not the Teensy regulator), common ground.
- **Interrupt safety:** WS2812Serial (not NeoPixel); keep OLED/SD out of the audio path and the timing-critical path.
- **Gain staging:** four summed voices clip past full scale — keep headroom and limit on the master.
- **Trigger latency:** prefer RAM/flash samples over SD for snappy hits; keep the control loop fast.
- **Timing:** the scheduler must be sample-accurate and non-blocking; derive from the audio clock, not `loop()` rate.
- **CPU/RAM budget:** monitor `AudioProcessorUsageMax()` / `AudioMemoryUsageMax()` as effects are added (Freeverb is the heaviest common object).

---

## 9. Open decisions (resolve before/early in build)

- Sample residence: flash vs SD vs RAM (affects latency and kit-swapping).
- Number of pattern slots and whether song mode ships in v1.
- MIDI clock/USB-MIDI sync: in scope or not? (Cheap to design in early.)
- Reverb/delay sends: include in v1 or defer.
- Recorded-sample length cap and storage format.
