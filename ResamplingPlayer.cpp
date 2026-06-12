#include <Audio.h>          // AudioNoInterrupts()/AudioInterrupts() macros
#include "ResamplingPlayer.h"

// rate = pitchRatio * sourceRate / outputRate (spec §4).
// AUDIO_SAMPLE_RATE_EXACT is ~44117.6 Hz on Teensy 3.6 — never hardcode 44100.
void ResamplingPlayer::updateRateFixed() {
  float  base = (float)sample.sampleRate / AUDIO_SAMPLE_RATE_EXACT;
  double eff  = (double)pitchRatio * base;
  rateFixed   = (uint64_t)(eff * 4294967296.0);   // * 2^32
}

void ResamplingPlayer::play(const Sample& s) {
  AudioNoInterrupts();
  sample = s;
  pos    = 0;
  updateRateFixed();
  // need idx+1 for interpolation, so a playable sample has >= 2 points
  playing = (s.data != nullptr && s.length >= 2);
  AudioInterrupts();
}

void ResamplingPlayer::stop() {
  AudioNoInterrupts();
  playing = false;
  AudioInterrupts();
}

void ResamplingPlayer::setPlaybackRate(float ratio) {  // safe mid-note
  AudioNoInterrupts();
  pitchRatio = ratio;
  updateRateFixed();
  AudioInterrupts();
}

void ResamplingPlayer::update() {
  if (!playing) return;                       // idle => silence downstream

  audio_block_t* block = allocate();
  if (!block) return;                         // out of audio memory: skip

  const int16_t* data      = sample.data;
  uint32_t       lastIndex = sample.length - 1;   // idx+1 used below

  int i = 0;
  for (; i < AUDIO_BLOCK_SAMPLES; i++) {
    uint32_t idx = (uint32_t)(pos >> 32);
    if (idx >= lastIndex) break;              // reached the end

    int32_t  a    = data[idx];
    int32_t  b    = data[idx + 1];
    uint32_t frac = (uint32_t)((pos >> 16) & 0xFFFF);   // 0..65535

    // linear interpolation; int64 product avoids 32-bit overflow (spec §6)
    int32_t s = a + (int32_t)(((int64_t)(b - a) * frac) >> 16);
    block->data[i] = (int16_t)s;

    pos += rateFixed;
  }

  // zero any remainder past the end, then stop
  for (; i < AUDIO_BLOCK_SAMPLES; i++) block->data[i] = 0;
  if ((uint32_t)(pos >> 32) >= lastIndex) playing = false;

  transmit(block);
  release(block);
}
