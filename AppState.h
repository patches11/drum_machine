#ifndef APPSTATE_H
#define APPSTATE_H

// Core data structures — verbatim from drum_machine_software_plan.md §3,
// plus the mode state machine and app-wide globals.

#include <stdint.h>
#include "Config.h"

struct Step {
  bool    active;     // hit present?
  bool    accent;     // accent flag (set if accent held on entry)
  uint8_t velocity;   // 0..127 base velocity
  int8_t  pitch;      // semitone offset (from 12-key board / octave shift)
  int8_t  nudge;      // per-step micro-timing offset, ms
};

struct Voice {
  Step     steps[NUM_STEPS];
  uint8_t  sampleId;        // SampleStore slot
  int8_t   rootSemis;       // coarse tune (octave calibration)
  int8_t   fineCents;       // fine tune, -100..+100
  uint8_t  level;           // mix level 0..127
  uint8_t  decay;           // amp-envelope decay 0..127
  uint8_t  filterCut;       // lowpass cutoff 0..127 (127 = open)
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
  uint8_t  volume;          // 1..10 -> headphone + lineOutLevel (see AudioEngine)
  uint8_t  micGain;         // 0..63 dB (SGTL5000 mic preamp, M7 sampling)
  uint8_t  masterFilterCut; // 0..127 master LP sweep, 127 = open (M8)
  uint8_t  reverbSend;      // 0..127 wet amount; only if FEATURE_REVERB_SEND
};

// Song-mode chain (M10): an ordered list of pattern slots the scheduler
// steps through, one entry per bar. Persisted in EEPROM with the globals
// (entries/count survive a power-cycle; `active` always boots false —
// the machine never wakes up mid-song).
struct ChainState {
  uint8_t entries[CHAIN_MAX];  // pattern slot per bar
  uint8_t count;               // 0 = empty
  uint8_t pos;                 // next entry to play (advanced by Scheduler)
  bool    active;              // chain playback engaged?
};

// UI modes per plan §5 ("Modes (UI)")
enum AppMode : uint8_t {
  MODE_HOME = 0,        // Home / Mix
  MODE_PATTERN_EDIT,
  MODE_SOUND_EDIT,
  MODE_TEMPO_SWING,
  MODE_FEEL,
  MODE_SAMPLE_RECORD,
  APP_MODE_COUNT
};

// Editing context (cursor position, selected voice) — UI state, not saved.
struct EditState {
  uint8_t cursor;       // step column 0..NUM_STEPS-1
  uint8_t voice;        // selected voice row 0..NUM_VOICES-1
  int8_t  octaveShift;  // -1/0/+1 octaves applied to key entry (M4)
};

extern Pattern     pattern;       // the WORKING pattern (edited + played)
extern Pattern     patternBank[NUM_PATTERN_SLOTS];  // M10 slot storage
extern ChainState  chain;
extern GlobalState globalState;
extern AppMode     appMode;
extern EditState   editState;

// Reset everything to power-on defaults (called before EEPROM/SD restore).
void appStateInit();

// M10 pattern slots: edits always happen in `pattern`; store writes it back
// to its bank slot, load pulls a slot into `pattern` (and sets
// globalState.currentPattern). Callers re-apply voice params after a load.
void patternBankStore();
void patternBankLoad(uint8_t slot);

#endif // APPSTATE_H
