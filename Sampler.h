#ifndef SAMPLER_H
#define SAMPLER_H

// Sampler — mic recording into the RAM pool (M7, plan Phase 7).
//
// Flow:
//   toggleArm() while idle   -> ARMED: record queue runs, waiting for sound
//   input exceeds RECORD_THRESHOLD -> RECORDING from that exact sample
//   partition full OR toggleArm()  -> finish: trim tail, normalize,
//        SampleStore.assignRam(slot), assign to the selected voice
//   toggleArm() while armed  -> cancel
//   leaving RECORD mode      -> cancel (update() self-checks appMode)
//
// Storage: a static 128 KB pool split into NUM_RECORD_SLOTS fixed
// partitions (~0.74 s each), used round-robin — never freed, never
// fragmented. The 44.1 kHz stream is decimated by 2 (pair-averaged) on
// the way in; the descriptor carries rate ≈ 22059 Hz and the
// ResamplingPlayer's rate correction keeps playback pitch-correct.
//
// The mic input graph (I2S in -> peak analyzer + record queue) lives in
// Sampler.cpp; codec input select + mic gain are AudioEngine's job.

#include <stdint.h>
#include "Config.h"

enum SamplerState : uint8_t { SMP_IDLE = 0, SMP_ARMED, SMP_RECORDING };

class SamplerClass {
public:
  void begin();
  void update();      // drain the record queue + level meter; every loop
  void toggleArm();   // IDLE->arm, ARMED->cancel, RECORDING->finish

  SamplerState state() const { return st; }
  uint8_t      level() const { return lastLevel; }  // input meter 0..100

private:
  void cancel();
  void finish();

  SamplerState st        = SMP_IDLE;
  uint8_t      recSlot   = 0;   // round-robin pool partition 0..3
  uint32_t     recLen    = 0;   // decimated samples written this take
  uint8_t      lastLevel = 0;   // most recent peak, 0..100
};

extern SamplerClass Sampler;

#endif // SAMPLER_H
