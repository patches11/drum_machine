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
// M4: voices are ResamplingPlayer (resampling_player_spec.md) — trigger()
// computes playbackRate = 2^((stepPitch + rootSemis)/12) * 2^(fineCents/1200)
// and the player folds in the sample-rate correction.

#include <stdint.h>
#include "Config.h"
#include "AppState.h"

class AudioEngineClass {
public:
  void begin();

  // Fire a voice. velocity 0..127; stepPitch in semitones from the key/step.
  // Handles pitch + choke groups per resampling_player_spec.md §7.
  void trigger(uint8_t voice, uint8_t velocity, int8_t stepPitch);

  // Push a voice's level/decay/filterCut from AppState into the graph
  // (call after editing voice params).
  void applyVoiceParams(uint8_t voice);

  // Master volume 1..10 -> headphone volume + PAM8302 lineOutLevel.
  void setVolume(uint8_t vol1to10);

  // Mic preamp gain 0..63 dB (M7 sampling; codec owned here).
  void setMicGain(uint8_t gainDb);

  // Stop any voice currently playing the given sample slot
  // (required before SampleStore::assignRam repoints it).
  void stopVoicesUsing(uint8_t sampleSlot);

  // Serial telemetry: CPU %, AudioMemory blocks, maxima.
  void printUsage();
  void resetUsageMax();
};

extern AudioEngineClass AudioEngine;

#endif // AUDIOENGINE_H
