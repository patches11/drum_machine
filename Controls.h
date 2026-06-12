#ifndef CONTROLS_H
#define CONTROLS_H

// Controls — maps raw input (matrix events, encoder deltas) to actions,
// dispatched by the current AppMode.
//
// M5: encoders are MODELESS via one binding table (MODE_BINDINGS). The same
// table drives BOTH the encoder dispatch and the OLED column labels/values,
// so the knob labels can never disagree with what the knobs do.
//
// Per-mode encoder bindings (Enc1..Enc4 = OLED columns left..right):
//   HOME/MIX     : VOICE  LEVEL  VOL    BPM
//   PATTERN_EDIT : STEP   VOICE  VEL    BPM
//   SOUND_EDIT   : SMPL   TUNE*  DECAY  FILT     (*Enc2 push: coarse/fine)
//   TEMPO_SWING  : BPM    SWING  SUBDV  ACCNT
//   FEEL         : VOICE  PUSH   NUDGE  HUMAN
//   SAMPLE_REC   : VOICE  MGAIN  SMPL   BPM      (Accent = arm/stop take)
//
// Buttons (all modes): TRANSPORT, MODE (next mode), OCTAVE.
// PATTERN_EDIT: 12 keys enter/remove hits (typewriter advance), ACCENT
//   held = accented entry / tap = toggle. Other modes: keys play the
//   selected voice live (no pattern edit).
//
// Actions are public so the Serial debug bridge (main sketch) can drive
// the exact same code paths before the matrix/encoders are wired.

#include <stdint.h>
#include <stddef.h>
#include "Config.h"
#include "AppState.h"
#include "InputMatrix.h"

// Everything an encoder can be bound to. One enum value = one parameter:
// its adjust behavior, label, value text and bar all live in Controls.cpp.
enum Param : uint8_t {
  P_NONE = 0,
  P_VOICE, P_LEVEL, P_VOL,   P_BPM,
  P_STEP,  P_VEL,
  P_SMPL,  P_TUNE,  P_DECAY, P_FILT,
  P_SWING, P_SUBDIV, P_ACCNT,
  P_PUSH,  P_NUDGE, P_HUMAN,
  P_MGAIN,
};

class ControlsClass {
public:
  void update();              // drain inputs, dispatch; call every loop

  // --- the modeless binding table (shared with Display) ---
  static const Param* bindings(AppMode m);          // 4 entries
  void        adjustParam(Param p, int detents);
  const char* paramLabel(Param p) const;            // <= 5 chars
  void        paramValue(Param p, char* buf, size_t n) const;
  int         paramBar(Param p) const;              // 0..100, -1 = no bar

  // --- actions (PATTERN_EDIT) ---
  void actionKey(uint8_t semitone);       // note key 0..11
  void actionMoveCursor(int delta);
  void actionSelectVoice(int delta);
  void actionAdjustVelocity(int delta);
  void actionToggleAccent();
  void actionTransport();
  void actionAdjustTempo(int delta);

  // --- M4 pitch/tuning ---
  void actionOctave();                    // cycle 0 -> +1 -> -1 -> 0
  void actionAdjustPitch(int delta);      // step pitch at cursor, semitones
  void actionAdjustRootTune(int delta);   // selected voice coarse, semitones
  void actionAdjustFineTune(int delta);   // selected voice fine, cents
  void actionSelectSample(int delta);     // selected voice: next/prev slot

  // --- M5 sound/groove params ---
  void actionAdjustLevel(int delta);      // voice level 0..127
  void actionAdjustDecay(int delta);      // 0..127 -> envelope
  void actionAdjustFilter(int delta);     // 0..127 -> LP cutoff
  void actionAdjustVolume(int delta);     // master 1..10
  void actionAdjustSwing(int delta);      // 50..75 %
  void actionToggleSubdiv();              // 8th <-> 16th
  void actionAdjustAccentBoost(int delta);// 0..50
  void actionAdjustPushPull(int delta);   // voice -20..+20 ms
  void actionAdjustNudge(int delta);      // step  -20..+20 ms
  void actionAdjustHumanize(int delta);   // 0..15 ms
  void actionAdjustMicGain(int delta);    // M7: mic preamp 0..63 dB
  void actionModeNext();                  // MODE button

  // UI needs redraw? (consumes the flag)
  bool consumeDirty();

private:
  void handleButton(const ButtonEvent& ev);
  void handleEncoderPush(uint8_t encIdx);
  void audition();                        // selected voice, only when stopped

  bool    uiDirty            = true;
  bool    accentUsedAsChord  = false;  // key entered while ACCENT held
  bool    tuneFine           = false;  // SOUND_EDIT Enc2: coarse st / fine ¢
  uint8_t lastVelocity       = 100;    // velocity applied to new hits
};

extern ControlsClass Controls;

#endif // CONTROLS_H
