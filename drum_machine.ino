// ============================================================================
// Teensy 3.6 Drum Machine — main sketch
//
// Wiring only: setup() initializes modules, loop() pumps them.
// All logic lives in the module .h/.cpp files. Specs in the .md docs.
//
// Milestone status: M1 (audio engine, fixed pitch, Serial-triggered).
//   The old hardware-test-rig sketch is preserved at tests/test_rig/.
//
// M1 Serial commands (115200):
//   1..4 : trigger voice 1-4 at full velocity (kick/snare/hat/clap)
//   q..r : (q,w,e,r) trigger voice 1-4 at half velocity
//   a    : trigger ALL voices at full velocity (clip/headroom test)
//   +/-  : master volume up/down (1..10)
//   u    : CPU + AudioMemory usage     (record these — budget baseline)
//   z    : reset usage maximums
// ============================================================================

#include "Config.h"
#include "AppState.h"
#include "SampleStore.h"
#include "AudioEngine.h"

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  appStateInit();
  SampleStore.begin();
  AudioEngine.begin();

  Serial.println("=== drum machine (M1) ===");
  Serial.print("Kit:");
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    Serial.print(" ");
    Serial.print(v + 1);
    Serial.print("=");
    Serial.print(SampleStore.name(pattern.voices[v].sampleId));
  }
  Serial.println();
  Serial.println("1-4 trigger | qwer half-vel | a=all | +/- vol | u=usage | z=reset");
}

void loop() {
  if (!Serial.available()) return;
  char cmd = Serial.read();

  switch (cmd) {
    case '1': case '2': case '3': case '4':
      AudioEngine.trigger(cmd - '1', 127, 0);
      break;
    case 'q': AudioEngine.trigger(0, 64, 0); break;
    case 'w': AudioEngine.trigger(1, 64, 0); break;
    case 'e': AudioEngine.trigger(2, 64, 0); break;
    case 'r': AudioEngine.trigger(3, 64, 0); break;
    case 'a':
      for (uint8_t v = 0; v < NUM_VOICES; v++) AudioEngine.trigger(v, 127, 0);
      Serial.println("all voices (headroom test)");
      break;
    case '+': case '=':
      AudioEngine.setVolume(globalState.volume + 1);
      Serial.print("volume "); Serial.println(globalState.volume);
      break;
    case '-':
      AudioEngine.setVolume(globalState.volume - 1);
      Serial.print("volume "); Serial.println(globalState.volume);
      break;
    case 'u': AudioEngine.printUsage(); break;
    case 'z': AudioEngine.resetUsageMax(); Serial.println("max reset"); break;
  }
}
