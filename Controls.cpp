#include <Arduino.h>
#include "Controls.h"
#include "AppState.h"
#include "AudioEngine.h"
#include "Sequencer.h"
#include "Encoders.h"
#include "SampleStore.h"
#include "Display.h"

ControlsClass Controls;

// ---------------------------------------------------------------------------
// The modeless binding table — Enc1..Enc4 = OLED columns left..right.
// Display renders labels/values from this same table (never hand-synced).

static const Param MODE_BINDINGS[APP_MODE_COUNT][4] = {
  /* MODE_HOME          */ { P_VOICE, P_LEVEL, P_VOL,    P_BPM   },
  /* MODE_PATTERN_EDIT  */ { P_STEP,  P_VOICE, P_VEL,    P_BPM   },
  /* MODE_SOUND_EDIT    */ { P_SMPL,  P_TUNE,  P_DECAY,  P_FILT  },
  /* MODE_TEMPO_SWING   */ { P_BPM,   P_SWING, P_SUBDIV, P_ACCNT },
  /* MODE_FEEL          */ { P_VOICE, P_PUSH,  P_NUDGE,  P_HUMAN },
  /* MODE_SAMPLE_RECORD */ { P_NONE,  P_NONE,  P_NONE,   P_NONE  },  // M7
};

const Param* ControlsClass::bindings(AppMode m) {
  return MODE_BINDINGS[m < APP_MODE_COUNT ? m : MODE_HOME];
}

static const char* const MODE_NAMES[APP_MODE_COUNT] = {
  "HOME", "PATTERN", "SOUND", "TEMPO", "FEEL", "RECORD",
};

bool ControlsClass::consumeDirty() {
  bool d = uiDirty;
  uiDirty = false;
  return d;
}

// Audition the selected voice — but never stack extra triggers on top of a
// running pattern (you hear edits through the pattern itself then).
void ControlsClass::audition() {
  if (!Sequencer.isRunning())
    AudioEngine.trigger(editState.voice, lastVelocity, 0);
}

// ---------------------------------------------------------------------------
// Actions

void ControlsClass::actionKey(uint8_t semitone) {
  if (semitone >= NUM_KEYS) return;
  int8_t pitch = (int8_t)semitone + editState.octaveShift * 12;

  if (appMode != MODE_PATTERN_EDIT) {
    // live playing: keys audition the selected voice, no pattern edit
    AudioEngine.trigger(editState.voice, lastVelocity, pitch);
    return;
  }

  Step& st = pattern.voices[editState.voice].steps[editState.cursor];

  if (st.active && st.pitch == pitch) {
    // same key on an existing hit: remove it (stay put for re-entry)
    st.active = false;
  } else {
    st.active   = true;
    st.pitch    = pitch;
    st.velocity = lastVelocity;
    if (InputMatrix.isHeld(BTN_ACCENT)) {
      st.accent = true;
      accentUsedAsChord = true;
    }
    // live feel: hear the hit as you place it
    int vel = st.velocity;
    if (st.accent) vel += globalState.accentBoost;
    if (vel > 127) vel = 127;
    AudioEngine.trigger(editState.voice, (uint8_t)vel, st.pitch);

    // typewriter advance
    editState.cursor = (editState.cursor + 1) % pattern.length;
  }
  uiDirty = true;
}

void ControlsClass::actionMoveCursor(int delta) {
  int c = (int)editState.cursor + delta;
  while (c < 0) c += pattern.length;
  editState.cursor = (uint8_t)(c % pattern.length);
  uiDirty = true;
}

void ControlsClass::actionSelectVoice(int delta) {
  int v = (int)editState.voice + delta;
  while (v < 0) v += NUM_VOICES;
  editState.voice = (uint8_t)(v % NUM_VOICES);
  char msg[20];
  snprintf(msg, sizeof(msg), "V%u %s", editState.voice + 1,
           SampleStore.name(pattern.voices[editState.voice].sampleId));
  Display.flash(msg);
  uiDirty = true;
}

void ControlsClass::actionAdjustVelocity(int delta) {
  Step& st = pattern.voices[editState.voice].steps[editState.cursor];
  if (!st.active) return;
  int v = (int)st.velocity + delta;
  if (v < 1)   v = 1;
  if (v > 127) v = 127;
  st.velocity  = (uint8_t)v;
  lastVelocity = st.velocity;            // new hits inherit the last edit
  uiDirty = true;
}

void ControlsClass::actionToggleAccent() {
  Step& st = pattern.voices[editState.voice].steps[editState.cursor];
  if (!st.active) return;
  st.accent = !st.accent;
  uiDirty = true;
}

void ControlsClass::actionTransport() {
  Sequencer.toggle();
  uiDirty = true;
}

void ControlsClass::actionAdjustTempo(int delta) {
  int bpm = (int)globalState.bpm + delta;
  if (bpm < 40)  bpm = 40;
  if (bpm > 240) bpm = 240;
  globalState.bpm = (uint16_t)bpm;
  uiDirty = true;
}

// ---------------------------------------------------------------------------
// M4 pitch/tuning

void ControlsClass::actionOctave() {
  // cycle 0 -> +1 -> -1 -> 0 (applies to subsequent key entry)
  editState.octaveShift = (editState.octaveShift == 0) ? 1
                        : (editState.octaveShift > 0)  ? -1 : 0;
  char msg[12];
  snprintf(msg, sizeof(msg), "OCT %+d", editState.octaveShift);
  Display.flash(msg);
  uiDirty = true;
}

void ControlsClass::actionAdjustPitch(int delta) {
  Step& st = pattern.voices[editState.voice].steps[editState.cursor];
  if (!st.active) return;
  int p = (int)st.pitch + delta;
  if (p < -24) p = -24;
  if (p >  24) p =  24;
  st.pitch = (int8_t)p;
  // hear the new pitch immediately (pattern plays it anyway when running)
  if (!Sequencer.isRunning())
    AudioEngine.trigger(editState.voice, st.velocity, st.pitch);
  uiDirty = true;
}

void ControlsClass::actionAdjustRootTune(int delta) {
  Voice& vc = pattern.voices[editState.voice];
  int r = (int)vc.rootSemis + delta;
  if (r < -24) r = -24;
  if (r >  24) r =  24;
  vc.rootSemis = (int8_t)r;
  audition();
  uiDirty = true;
}

void ControlsClass::actionAdjustFineTune(int delta) {
  Voice& vc = pattern.voices[editState.voice];
  int f = (int)vc.fineCents + delta;
  if (f < -100) f = -100;
  if (f >  100) f =  100;
  vc.fineCents = (int8_t)f;
  audition();
  uiDirty = true;
}

void ControlsClass::actionSelectSample(int delta) {
  Voice& vc = pattern.voices[editState.voice];
  int dir = (delta < 0) ? -1 : 1;
  uint8_t n = SampleStore.slotCount();
  // step to the next non-empty slot (wraps; gives up after a full lap)
  uint8_t id = vc.sampleId;
  for (uint8_t tries = 0; tries < n; tries++) {
    id = (uint8_t)((id + n + dir) % n);
    if (SampleStore.source(id) != SRC_NONE) break;
  }
  if (SampleStore.source(id) == SRC_NONE) return;
  vc.sampleId = id;
  Display.flash(SampleStore.name(id));
  audition();
  uiDirty = true;
}

// ---------------------------------------------------------------------------
// M5 sound/groove params

void ControlsClass::actionAdjustLevel(int delta) {
  Voice& vc = pattern.voices[editState.voice];
  int l = (int)vc.level + delta;
  if (l < 0)   l = 0;
  if (l > 127) l = 127;
  vc.level = (uint8_t)l;
  audition();
  uiDirty = true;
}

void ControlsClass::actionAdjustDecay(int delta) {
  Voice& vc = pattern.voices[editState.voice];
  int d = (int)vc.decay + delta;
  if (d < 0)   d = 0;
  if (d > 127) d = 127;
  vc.decay = (uint8_t)d;
  AudioEngine.applyVoiceParams(editState.voice);
  audition();
  uiDirty = true;
}

void ControlsClass::actionAdjustFilter(int delta) {
  Voice& vc = pattern.voices[editState.voice];
  int f = (int)vc.filterCut + delta;
  if (f < 0)   f = 0;
  if (f > 127) f = 127;
  vc.filterCut = (uint8_t)f;
  AudioEngine.applyVoiceParams(editState.voice);
  audition();
  uiDirty = true;
}

void ControlsClass::actionAdjustVolume(int delta) {
  AudioEngine.setVolume((uint8_t)((int)globalState.volume + delta));
  uiDirty = true;
}

void ControlsClass::actionAdjustSwing(int delta) {
  int s = (int)globalState.swing + delta;
  if (s < 50) s = 50;
  if (s > 75) s = 75;
  globalState.swing = (uint8_t)s;
  uiDirty = true;
}

void ControlsClass::actionToggleSubdiv() {
  globalState.swingSubdiv = (globalState.swingSubdiv == 16) ? 8 : 16;
  uiDirty = true;
}

void ControlsClass::actionAdjustAccentBoost(int delta) {
  int a = (int)globalState.accentBoost + delta;
  if (a < 0)  a = 0;
  if (a > 50) a = 50;
  globalState.accentBoost = (uint8_t)a;
  uiDirty = true;
}

void ControlsClass::actionAdjustPushPull(int delta) {
  Voice& vc = pattern.voices[editState.voice];
  int p = (int)vc.pushPull + delta;
  if (p < -20) p = -20;
  if (p >  20) p =  20;
  vc.pushPull = (int8_t)p;
  uiDirty = true;
}

void ControlsClass::actionAdjustNudge(int delta) {
  Step& st = pattern.voices[editState.voice].steps[editState.cursor];
  if (!st.active) return;
  int nd = (int)st.nudge + delta;
  if (nd < -20) nd = -20;
  if (nd >  20) nd =  20;
  st.nudge = (int8_t)nd;
  uiDirty = true;
}

void ControlsClass::actionAdjustHumanize(int delta) {
  int h = (int)globalState.humanize + delta;
  if (h < 0)  h = 0;
  if (h > 15) h = 15;
  globalState.humanize = (uint8_t)h;
  uiDirty = true;
}

void ControlsClass::actionModeNext() {
  // cycle HOME -> PATTERN -> SOUND -> TEMPO -> FEEL -> HOME
  // (SAMPLE_RECORD joins the cycle at M7)
  appMode = (AppMode)(appMode + 1);
  if (appMode >= MODE_SAMPLE_RECORD) appMode = MODE_HOME;
  Display.flash(MODE_NAMES[appMode]);
  uiDirty = true;
}

// ---------------------------------------------------------------------------
// Param dispatch: adjust + label + value + bar, all from one table

void ControlsClass::adjustParam(Param p, int d) {
  switch (p) {
    case P_VOICE:  actionSelectVoice(d);        break;
    case P_LEVEL:  actionAdjustLevel(d * 4);    break;
    case P_VOL:    actionAdjustVolume(d);       break;
    case P_BPM:    actionAdjustTempo(d);        break;
    case P_STEP:   actionMoveCursor(d);         break;
    case P_VEL:    actionAdjustVelocity(d * 4); break;
    case P_SMPL:   actionSelectSample(d);       break;
    case P_TUNE:   if (tuneFine) actionAdjustFineTune(d * 5);
                   else          actionAdjustRootTune(d);
                   break;
    case P_DECAY:  actionAdjustDecay(d * 4);    break;
    case P_FILT:   actionAdjustFilter(d * 4);   break;
    case P_SWING:  actionAdjustSwing(d);        break;
    case P_SUBDIV: actionToggleSubdiv();        break;
    case P_ACCNT:  actionAdjustAccentBoost(d * 2); break;
    case P_PUSH:   actionAdjustPushPull(d);     break;
    case P_NUDGE:  actionAdjustNudge(d);        break;
    case P_HUMAN:  actionAdjustHumanize(d);     break;
    default: break;
  }
}

const char* ControlsClass::paramLabel(Param p) const {
  switch (p) {
    case P_VOICE:  return "VOICE";
    case P_LEVEL:  return "LEVEL";
    case P_VOL:    return "VOL";
    case P_BPM:    return "BPM";
    case P_STEP:   return "STEP";
    case P_VEL:    return "VEL";
    case P_SMPL:   return "SMPL";
    case P_TUNE:   return tuneFine ? "FINE" : "TUNE";
    case P_DECAY:  return "DECAY";
    case P_FILT:   return "FILT";
    case P_SWING:  return "SWING";
    case P_SUBDIV: return "SUBDV";
    case P_ACCNT:  return "ACCNT";
    case P_PUSH:   return "PUSH";
    case P_NUDGE:  return "NUDGE";
    case P_HUMAN:  return "HUMAN";
    default:       return "";
  }
}

void ControlsClass::paramValue(Param p, char* buf, size_t n) const {
  const Voice& vc = pattern.voices[editState.voice];
  const Step&  st = vc.steps[editState.cursor];
  switch (p) {
    case P_VOICE:  snprintf(buf, n, "%u", editState.voice + 1);        break;
    case P_LEVEL:  snprintf(buf, n, "%u", vc.level);                   break;
    case P_VOL:    snprintf(buf, n, "%u", globalState.volume);         break;
    case P_BPM:    snprintf(buf, n, "%u", globalState.bpm);            break;
    case P_STEP:   snprintf(buf, n, "%u/%u", editState.cursor + 1,
                            pattern.length);                           break;
    case P_VEL:    if (st.active) snprintf(buf, n, "%u", st.velocity);
                   else           snprintf(buf, n, "-");               break;
    case P_SMPL:   snprintf(buf, n, "%s", SampleStore.name(vc.sampleId)); break;
    case P_TUNE:   if (tuneFine) snprintf(buf, n, "%+dc",  vc.fineCents);
                   else          snprintf(buf, n, "%+dst", vc.rootSemis);
                   break;
    case P_DECAY:  snprintf(buf, n, "%u", vc.decay);                   break;
    case P_FILT:   snprintf(buf, n, "%u", vc.filterCut);               break;
    case P_SWING:  snprintf(buf, n, "%u%%", globalState.swing);        break;
    case P_SUBDIV: snprintf(buf, n, "%s",
                            globalState.swingSubdiv == 16 ? "16th" : "8th"); break;
    case P_ACCNT:  snprintf(buf, n, "+%u", globalState.accentBoost);   break;
    case P_PUSH:   snprintf(buf, n, "%+dms", vc.pushPull);             break;
    case P_NUDGE:  if (st.active) snprintf(buf, n, "%+dms", st.nudge);
                   else           snprintf(buf, n, "-");               break;
    case P_HUMAN:  snprintf(buf, n, "%ums", globalState.humanize);     break;
    default:       buf[0] = 0;                                         break;
  }
}

int ControlsClass::paramBar(Param p) const {
  const Voice& vc = pattern.voices[editState.voice];
  const Step&  st = vc.steps[editState.cursor];
  switch (p) {
    case P_LEVEL:  return vc.level * 100 / 127;
    case P_VOL:    return (globalState.volume - 1) * 100 / 9;
    case P_VEL:    return st.active ? st.velocity * 100 / 127 : -1;
    case P_DECAY:  return vc.decay * 100 / 127;
    case P_FILT:   return vc.filterCut * 100 / 127;
    case P_SWING:  return (globalState.swing - 50) * 100 / 25;
    case P_ACCNT:  return globalState.accentBoost * 100 / 50;
    case P_HUMAN:  return globalState.humanize * 100 / 15;
    default:       return -1;
  }
}

// ---------------------------------------------------------------------------
// Dispatch

void ControlsClass::handleEncoderPush(uint8_t encIdx) {
  Param p = bindings(appMode)[encIdx];
  switch (p) {
    case P_TUNE:                       // coarse semitones <-> fine cents
      tuneFine = !tuneFine;
      Display.flash(tuneFine ? "FINE c" : "TUNE st");
      uiDirty = true;
      break;
    case P_VOICE:                      // push = audition the selection
    case P_SMPL:
      audition();
      break;
    default:
      break;
  }
}

void ControlsClass::handleButton(const ButtonEvent& ev) {
  // global buttons, any mode
  if (ev.type == BTN_PRESS) {
    switch (ev.id) {
      case BTN_TRANSPORT: actionTransport(); return;
      case BTN_MODE:      actionModeNext();  return;
      case BTN_OCTAVE:    actionOctave();    return;
      case BTN_ENC1: case BTN_ENC2: case BTN_ENC3: case BTN_ENC4:
        handleEncoderPush(ev.id - BTN_ENC1); return;
      default: break;
    }
  }

  switch (appMode) {
    case MODE_PATTERN_EDIT:
      if (ev.type == BTN_PRESS && ev.id < NUM_KEYS) {
        actionKey(ev.id);
      } else if (ev.id == BTN_ACCENT) {
        if (ev.type == BTN_PRESS) {
          accentUsedAsChord = false;
        } else if (ev.type == BTN_RELEASE && !accentUsedAsChord) {
          actionToggleAccent();         // tap (no key chorded) = toggle
        }
      }
      // BTN_SHIFT: M10
      break;

    default:
      // other modes: keys = live playing on the selected voice
      if (ev.type == BTN_PRESS && ev.id < NUM_KEYS) actionKey(ev.id);
      break;
  }
}

void ControlsClass::update() {
  // modeless encoders: dispatch through the current mode's binding table
  const Param* b = bindings(appMode);
  for (uint8_t i = 0; i < 4; i++) {
    int d = Encoders.detentDelta(i);
    if (d != 0) adjustParam(b[i], d);
  }

  ButtonEvent ev;
  while (InputMatrix.nextEvent(ev)) handleButton(ev);
}
