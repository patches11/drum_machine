#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

// AudioEngine — owns the audio graph and the SGTL5000 codec.
//
// Graph per drum_machine_software_plan.md §4:
//   voice[0..3]: player -> AudioEffectEnvelope -> AudioFilterStateVariable(LP)
//                -> mixer channel
//   mixer -> mono out (same signal to both I2S channels)
//   (master compressor/limiter inserts at M8; FX sends optional later)
//
// M1 note: the players are RawSamplePlayer (fixed pitch). At M4 they are
// replaced by ResamplingPlayer and trigger() gains real pitch handling —
// its signature already carries stepPitch for that reason.

#include <stdint.h>
#include "Config.h"
#include "AppState.h"

class AudioEngineClass {
public:
  void begin();

  // Fire a voice. velocity 0..127; stepPitch in semitones (ignored until M4).
  // Handles choke groups per resampling_player_spec.md §7.
  void trigger(uint8_t voice, uint8_t velocity, int8_t stepPitch);

  // Push a voice's level/decay/filterCut from AppState into the graph
  // (call after editing voice params).
  void applyVoiceParams(uint8_t voice);

  // Master volume 1..10 -> headphone volume + PAM8302 lineOutLevel.
  void setVolume(uint8_t vol1to10);

  // Stop any voice currently playing the given sample slot
  // (required before SampleStore::assignRam repoints it).
  void stopVoicesUsing(uint8_t sampleSlot);

  // Serial telemetry: CPU %, AudioMemory blocks, maxima.
  void printUsage();
  void resetUsageMax();
};

extern AudioEngineClass AudioEngine;

#endif // AUDIOENGINE_H
