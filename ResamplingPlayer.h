#ifndef RESAMPLINGPLAYER_H
#define RESAMPLINGPLAYER_H

// ResamplingPlayer — variable-rate one-shot sample source.
// Implemented per resampling_player_spec.md (M4). One instance per voice:
//
//   ResamplingPlayer -> AudioEffectEnvelope -> AudioFilterStateVariable -> mixer
//
// Does ONLY playback + pitch. Velocity->gain lives on the mixer, decay on
// the envelope, tone on the filter — all downstream.
//
//   * read position: 32.32 fixed-point in a uint64_t (spec §3) — no float
//     precision loss, cheap on the M4F
//   * rate = pitchRatio * (sample.sampleRate / AUDIO_SAMPLE_RATE_EXACT),
//     so 22.05 kHz sources play at correct pitch automatically (spec §4)
//   * linear interpolation between adjacent samples (spec §6)
//   * idle => transmits nothing (downstream reads silence, zero CPU)
//   * all ISR-shared state guarded with AudioNoInterrupts() (spec §5)
//   * play() while sounding restarts from 0 — desired mono retrigger

#include <Arduino.h>
#include <AudioStream.h>
#include "SampleStore.h"

class ResamplingPlayer : public AudioStream {
public:
  ResamplingPlayer() : AudioStream(0, nullptr) {}   // 0 inputs => source

  void play(const Sample& s);          // trigger from the start
  void stop();                         // silence immediately
  void setPlaybackRate(float ratio);   // 1.0 native, 2.0 +octave, 0.5 -octave
  bool isPlaying() const { return playing; }

  virtual void update() override;

private:
  void updateRateFixed();              // fold pitchRatio + rate correction

  Sample            sample;
  volatile bool     playing    = false;
  volatile uint64_t pos        = 0;    // 32.32 fixed-point read position
  volatile uint64_t rateFixed  = 0;    // 32.32 increment per output sample
  float             pitchRatio = 1.0f;
};

#endif // RESAMPLINGPLAYER_H
