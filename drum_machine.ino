// ============================================================================
// Teensy 3.6 Drum Machine — main sketch
//
// Wiring only: setup() initializes modules, loop() pumps them.
// All logic lives in the module .h/.cpp files. Specs in the .md docs.
//
// Milestone status: M6 (lookahead scheduler — groove is now audible).
//   The old hardware-test-rig sketch is preserved at tests/test_rig/.
//
// Serial commands (115200) — the editing commands call the SAME Controls
// actions the matrix/encoders do, so milestones are verifiable before
// they're wired:
//   p    : play / stop                          (= Transport)
//   m    : next mode                            (= Mode button)
//   d    : load demo pattern (four-on-the-floor)
//   x    : clear pattern
//   [ ]  : tempo -5 / +5 BPM
//   , .  : move edit cursor left / right
//   v    : select next voice
//   < >  : velocity -8 / +8 at cursor
//   k    : key-enter a hit at the cursor (root note, typewriter advance)
//   A    : toggle accent at cursor              (= Accent tap)
//   o    : octave shift cycle 0/+1/-1           (= Octave button)
//   ( )  : step pitch -1 / +1 semitone at cursor (preview plays)
//   r R  : voice coarse tune -1 / +1 semitone
//   f F  : voice fine tune -10 / +10 cents
//   s    : voice sample -> next slot (slot 4 = 22.05 kHz A440 test tone)
//   g G  : voice level -8 / +8
//   e E  : voice decay -8 / +8
//   c C  : voice filter cutoff -8 / +8
//   w W  : swing -1 / +1 %
//   b    : swing subdiv 8th/16th
//   y Y  : accent boost -4 / +4
//   j J  : voice push/pull -1 / +1 ms
//   n N  : step nudge -1 / +1 ms
//   h H  : humanize -1 / +1 ms
//   1..4 : trigger voice 1-4 manually at full velocity
//   a    : trigger ALL voices (headroom test)
//   +/-  : master volume (1..10)
//   u    : CPU + AudioMemory usage
//   z    : reset usage maximums
// ============================================================================

#include "Config.h"
#include "AppState.h"
#include "SampleStore.h"
#include "AudioEngine.h"
#include "Clock.h"
#include "Sequencer.h"
#include "Leds.h"
#include "InputMatrix.h"
#include "Encoders.h"
#include "Controls.h"
#include "Display.h"

static bool ledsDirty = true;

void loadDemoPattern() {
  for (uint8_t v = 0; v < NUM_VOICES; v++)
    for (uint8_t s = 0; s < NUM_STEPS; s++)
      pattern.voices[v].steps[s].active = false;

  // kick: four on the floor, accented on the 1
  for (uint8_t s = 0; s < NUM_STEPS; s += 4) {
    pattern.voices[0].steps[s].active   = true;
    pattern.voices[0].steps[s].velocity = 110;
  }
  pattern.voices[0].steps[0].accent = true;

  // snare: 2 and 4
  pattern.voices[1].steps[4].active    = true;
  pattern.voices[1].steps[4].velocity  = 105;
  pattern.voices[1].steps[12].active   = true;
  pattern.voices[1].steps[12].velocity = 105;

  // hats: 8ths, offbeats quieter
  for (uint8_t s = 0; s < NUM_STEPS; s += 2) {
    pattern.voices[2].steps[s].active   = true;
    pattern.voices[2].steps[s].velocity = (s % 4 == 0) ? 95 : 55;
  }

  ledsDirty = true;
  Display.markDirty();
  Serial.println("demo pattern loaded");
}

void clearPattern() {
  for (uint8_t v = 0; v < NUM_VOICES; v++)
    for (uint8_t s = 0; s < NUM_STEPS; s++) {
      pattern.voices[v].steps[s].active = false;
      pattern.voices[v].steps[s].accent = false;
    }
  ledsDirty = true;
  Display.markDirty();
  Serial.println("pattern cleared");
}

void printCursor() {
  const Voice& vc = pattern.voices[editState.voice];
  const Step&  st = vc.steps[editState.cursor];
  Serial.print("voice ");
  Serial.print(editState.voice + 1);
  Serial.print(" (");
  Serial.print(SampleStore.name(vc.sampleId));
  if (vc.rootSemis || vc.fineCents) {
    Serial.print(" tune ");
    Serial.print(vc.rootSemis);
    Serial.print("st ");
    Serial.print(vc.fineCents);
    Serial.print("c");
  }
  Serial.print(") oct ");
  Serial.print(editState.octaveShift);
  Serial.print(" step ");
  Serial.print(editState.cursor + 1);
  if (st.active) {
    Serial.print("  [hit vel ");
    Serial.print(st.velocity);
    Serial.print(" pitch ");
    Serial.print(st.pitch);
    if (st.accent) Serial.print(" ACCENT");
    Serial.print("]");
  } else {
    Serial.print("  [empty]");
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  appStateInit();
  SampleStore.begin();
  AudioEngine.begin();
  Display.begin();               // AFTER AudioEngine: codec owns I2C init
  Leds.begin();
  InputMatrix.begin();
  Encoders.begin();
  Sequencer.begin(&INTERNAL_CLOCK);

  Serial.println("=== drum machine (M6) ===");
  Serial.print("Kit:");
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    Serial.print(" ");
    Serial.print(v + 1);
    Serial.print("=");
    Serial.print(SampleStore.name(pattern.voices[v].sampleId));
  }
  Serial.println();
  Serial.println("p=play m=mode d=demo x=clear [ ]=tempo ,.=cursor v=voice <>=vel k=key");
  Serial.println("A=accent o=oct ()=pitch rR=tune fF=fine s=smpl gG=lvl eE=dcy cC=filt");
  Serial.println("wW=swing b=subdiv yY=accboost jJ=push nN=nudge hH=human 1-4 a +/- u z");
}

void handleSerial() {
  if (!Serial.available()) return;
  char cmd = Serial.read();

  switch (cmd) {
    case 'p':
      Controls.actionTransport();
      Serial.println(Sequencer.isRunning() ? "PLAY" : "STOP");
      break;
    case 'm': Controls.actionModeNext(); break;
    case 'd': loadDemoPattern(); break;
    case 'x': clearPattern(); break;
    case '[': Controls.actionAdjustTempo(-5);
              Serial.print("tempo "); Serial.println(globalState.bpm); break;
    case ']': Controls.actionAdjustTempo(+5);
              Serial.print("tempo "); Serial.println(globalState.bpm); break;

    // --- M3 editing bridge: same code paths as matrix/encoders ---
    case ',': Controls.actionMoveCursor(-1);    printCursor(); break;
    case '.': Controls.actionMoveCursor(+1);    printCursor(); break;
    case 'v': Controls.actionSelectVoice(+1);   printCursor(); break;
    case '<': Controls.actionAdjustVelocity(-8); printCursor(); break;
    case '>': Controls.actionAdjustVelocity(+8); printCursor(); break;
    case 'k': Controls.actionKey(0);            printCursor(); break;
    case 'A': Controls.actionToggleAccent();    printCursor(); break;

    // --- M4 pitch/tuning bridge ---
    case 'o': Controls.actionOctave();              printCursor(); break;
    case '(': Controls.actionAdjustPitch(-1);       printCursor(); break;
    case ')': Controls.actionAdjustPitch(+1);       printCursor(); break;
    case 'r': Controls.actionAdjustRootTune(-1);    printCursor(); break;
    case 'R': Controls.actionAdjustRootTune(+1);    printCursor(); break;
    case 'f': Controls.actionAdjustFineTune(-10);   printCursor(); break;
    case 'F': Controls.actionAdjustFineTune(+10);   printCursor(); break;
    case 's': Controls.actionSelectSample(+1);      printCursor(); break;

    // --- M5 sound/groove bridge ---
    case 'g': Controls.actionAdjustLevel(-8);       printCursor(); break;
    case 'G': Controls.actionAdjustLevel(+8);       printCursor(); break;
    case 'e': Controls.actionAdjustDecay(-8);       printCursor(); break;
    case 'E': Controls.actionAdjustDecay(+8);       printCursor(); break;
    case 'c': Controls.actionAdjustFilter(-8);      printCursor(); break;
    case 'C': Controls.actionAdjustFilter(+8);      printCursor(); break;
    case 'w': case 'W':
      Controls.actionAdjustSwing(cmd == 'w' ? -1 : +1);
      Serial.print("swing "); Serial.print(globalState.swing);
      Serial.print("% ("); Serial.print(globalState.swingSubdiv);
      Serial.println("th)");
      break;
    case 'b':
      Controls.actionToggleSubdiv();
      Serial.print("swing subdiv ");
      Serial.print(globalState.swingSubdiv); Serial.println("th");
      break;
    case 'y': case 'Y':
      Controls.actionAdjustAccentBoost(cmd == 'y' ? -4 : +4);
      Serial.print("accent boost +");
      Serial.println(globalState.accentBoost);
      break;
    case 'j': case 'J':
      Controls.actionAdjustPushPull(cmd == 'j' ? -1 : +1);
      Serial.print("push V"); Serial.print(editState.voice + 1);
      Serial.print(" ");
      Serial.print(pattern.voices[editState.voice].pushPull);
      Serial.println("ms");
      break;
    case 'n': Controls.actionAdjustNudge(-1);       printCursor(); break;
    case 'N': Controls.actionAdjustNudge(+1);       printCursor(); break;
    case 'h': case 'H':
      Controls.actionAdjustHumanize(cmd == 'h' ? -1 : +1);
      Serial.print("humanize ");
      Serial.print(globalState.humanize); Serial.println("ms");
      break;

    case '1': case '2': case '3': case '4':
      AudioEngine.trigger(cmd - '1', 127, 0);
      break;
    case 'a':
      for (uint8_t v = 0; v < NUM_VOICES; v++) AudioEngine.trigger(v, 127, 0);
      break;
    case '+': case '=':
      Controls.actionAdjustVolume(+1);
      Serial.print("volume "); Serial.println(globalState.volume);
      break;
    case '-':
      Controls.actionAdjustVolume(-1);
      Serial.print("volume "); Serial.println(globalState.volume);
      break;
    case 'u': AudioEngine.printUsage(); break;
    case 'z': AudioEngine.resetUsageMax(); Serial.println("max reset"); break;
  }
}

void loop() {
  // 1. Musical timing first — nothing above this may block.
  if (Sequencer.tick()) ledsDirty = true;

  // 2. Inputs: matrix scan, then Controls drains events + encoder deltas.
  InputMatrix.scan();
  Controls.update();
  handleSerial();
  if (Controls.consumeDirty()) {
    ledsDirty = true;
    Display.markDirty();
  }

  // 3. Feedback. LED show() is DMA — safe anytime. The OLED transfer
  //    blocks ~23 ms, so Display.update() self-gates against the sequencer.
  if (ledsDirty) {
    Leds.render(Sequencer.currentStep(), Sequencer.isRunning());
    ledsDirty = false;
  }
  Display.update();
}
