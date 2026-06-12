#include "AppState.h"
#include <string.h>

Pattern     pattern;
GlobalState globalState;
AppMode     appMode = MODE_PATTERN_EDIT;   // boot straight into editing (M3)
EditState   editState;

void appStateInit() {
  memset(&pattern, 0, sizeof(pattern));
  pattern.length = NUM_STEPS;

  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    Voice& vc   = pattern.voices[v];
    vc.sampleId  = v;          // slots 0..3 = PROGMEM kit: kick/snare/hat/clap
    vc.rootSemis = 0;
    vc.fineCents = 0;
    vc.level     = 100;
    vc.decay     = 100;
    vc.filterCut = 127;        // open
    vc.pushPull  = 0;
    vc.chokeGroup = 0;
    vc.mute = false;
    vc.solo = false;
    for (uint8_t s = 0; s < NUM_STEPS; s++) {
      vc.steps[s].velocity = 100;   // sensible default when a step is activated
    }
  }

  globalState.bpm            = 120;
  globalState.swing          = 50;   // straight
  globalState.swingSubdiv    = 16;
  globalState.accentBoost    = 27;   // 100 + 27 = full-scale accent
  globalState.humanize       = 0;
  globalState.currentPattern = 0;
  globalState.volume         = 6;
  globalState.micGain        = 36;

  appMode = MODE_PATTERN_EDIT;

  editState.cursor      = 0;
  editState.voice       = 0;
  editState.octaveShift = 0;
}
