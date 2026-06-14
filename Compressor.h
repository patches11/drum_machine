#ifndef COMPRESSOR_H
#define COMPRESSOR_H

// AudioEffectLimiter — master safety floor (M8, plan Phase 8).
//
// Sits between the master filter and the I2S output. Three stages:
//   1. fixed makeup gain (voice gains were lowered to buy headroom)
//   2. peak limiter: instant attack (no overshoot to catch), smooth
//      ~75 ms release, unity below threshold
//   3. cubic soft-clip floor: unity slope at 0, hard ceiling at +/-1 —
//      whatever still gets through saturates gently instead of cracking
//
// bypass(true) keeps stage 1 (same loudness) but replaces 2+3 with a
// hard clamp — an honest A/B of the protection, not of the gain staging.
//
// All work is per-sample float in update(); the M4F FPU does this in a
// fraction of a percent. reductionDb() is for the control loop ('u').

#include <AudioStream.h>

class AudioEffectLimiter : public AudioStream {
public:
  AudioEffectLimiter() : AudioStream(1, inputQueueArray) {}
  virtual void update(void);

  void  bypass(bool b)   { byp = b; }
  bool  bypassed() const { return byp; }

  float reductionDb();     // gain reduction in the last block, dB
  float reductionDbMax();  // worst since resetMax()
  void  resetMax();

private:
  audio_block_t* inputQueueArray[1];
  volatile bool  byp     = false;
  float          env     = 0.0f;  // peak envelope — audio ISR only
  volatile float gainNow = 1.0f;  // min gain in the last block
  volatile float gainMin = 1.0f;  // min gain since resetMax()
};

#endif // COMPRESSOR_H
