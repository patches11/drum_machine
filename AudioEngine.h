#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

// AudioEngine — owns the audio graph and the SGTL5000 codec.
//
// Graph per drum_machine_software_plan.md §4:
//   voice[0..3]: player -> AudioEffectEnvelope -> AudioFilterStateVariable(LP)
//                -> mixer channel
//   mixer -> masterBus -> master sweep filter -> limiter -> mono out
//   (same signal to both I2S channels; M8 master chain — see Compressor.h.
//    Freeverb send onto masterBus ch1 behind FEATURE_REVERB_SEND.)
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

  // M8: push globalState.masterFilterCut into the master sweep filter.
  void applyMasterParams();

  // M8: limiter A/B for the Serial bridge ('L'). GR shows in printUsage().
  void setLimiterBypass(bool b);
  bool limiterBypassed();

#if FEATURE_REVERB_SEND
  // M8: master reverb wet amount 0..127 (flag-gated; check CPU with 'u').
  void setReverbSend(uint8_t send);
#endif

  // Stop any voice currently playing the given sample slot
  // (required before SampleStore::assignRam repoints it).
  void stopVoicesUsing(uint8_t sampleSlot);

  // Serial telemetry: CPU %, AudioMemory blocks, maxima.
  void printUsage();
  void resetUsageMax();
};

extern AudioEngineClass AudioEngine;

#endif // AUDIOENGINE_H
