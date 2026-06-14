#include <Audio.h>
#include <math.h>
#include "Compressor.h"

// Tuning. MAKEUP restores the loudness given up by the lower VOICE_GAIN
// (AudioEngine.cpp) — the headroom the limiter actually works in.
static const float MAKEUP    = 1.4f;
static const float THRESHOLD = 0.90f;    // limit ceiling, fraction of FS
static const float RELEASE   = 0.0003f;  // per-sample; ~75 ms to 1/e

void AudioEffectLimiter::update(void) {
  audio_block_t* block = receiveWritable(0);
  if (!block) return;

  float e    = env;
  float gmin = 1.0f;

  for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
    float x = (float)block->data[i] * (MAKEUP / 32768.0f);

    if (!byp) {
      // peak envelope: instant attack, exponential release
      float ax = fabsf(x);
      if (ax > e) e = ax;
      else        e += (ax - e) * RELEASE;

      if (e > THRESHOLD) {
        float g = THRESHOLD / e;
        x *= g;
        if (g < gmin) gmin = g;
      }

      // cubic soft-clip floor: y = x - x^3/6.75 (unity slope at 0,
      // smooth saturation, exactly +/-1 at +/-1.5, hard beyond)
      if      (x >  1.5f) x =  1.0f;
      else if (x < -1.5f) x = -1.0f;
      else                x = x - (x * x * x) * (1.0f / 6.75f);
    } else {
      // bypass: same makeup, hard clamp — hear what the limiter prevents
      if      (x >  1.0f) x =  1.0f;
      else if (x < -1.0f) x = -1.0f;
    }

    block->data[i] = (int16_t)(x * 32767.0f);
  }

  env     = e;
  gainNow = gmin;
  if (gmin < gainMin) gainMin = gmin;

  transmit(block);
  release(block);
}

float AudioEffectLimiter::reductionDb() {
  float g = gainNow;
  return (g < 1.0f) ? -20.0f * log10f(g) : 0.0f;
}

float AudioEffectLimiter::reductionDbMax() {
  float g = gainMin;
  return (g < 1.0f) ? -20.0f * log10f(g) : 0.0f;
}

void AudioEffectLimiter::resetMax() {
  gainMin = 1.0f;
}
