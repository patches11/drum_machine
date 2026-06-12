#ifndef RAWSAMPLEPLAYER_H
#define RAWSAMPLEPLAYER_H

// RawSamplePlayer — M1 placeholder voice source.
//
// Plays a Sample (SampleStore.h) at fixed 1:1 rate. No pitch, no
// interpolation. Replaced by ResamplingPlayer (resampling_player_spec.md)
// at milestone M4 — the public interface (play/stop/isPlaying) is the
// same so the swap is drop-in.
//
// Note: assumes sample.sampleRate == the audio rate. The default kit is
// generated at 44100 Hz for exactly this reason; rate correction arrives
// with the ResamplingPlayer.

#include <Arduino.h>
#include <AudioStream.h>
#include "SampleStore.h"

class RawSamplePlayer : public AudioStream {
public:
  RawSamplePlayer() : AudioStream(0, nullptr) {}   // 0 inputs => source

  void play(const Sample& s) {
    AudioNoInterrupts();
    sample  = s;
    pos     = 0;
    playing = (s.data != nullptr && s.length > 0);
    AudioInterrupts();
  }

  void stop() {
    AudioNoInterrupts();
    playing = false;
    AudioInterrupts();
  }

  bool isPlaying() const { return playing; }

  virtual void update() override {
    if (!playing) return;                    // idle => silence downstream

    audio_block_t* block = allocate();
    if (!block) return;                      // out of audio memory: skip

    const int16_t* data = sample.data;
    uint32_t len = sample.length;

    int i = 0;
    for (; i < AUDIO_BLOCK_SAMPLES && pos < len; i++) {
      block->data[i] = data[pos++];
    }
    for (; i < AUDIO_BLOCK_SAMPLES; i++) block->data[i] = 0;
    if (pos >= len) playing = false;

    transmit(block);
    release(block);
  }

private:
  Sample            sample;
  volatile bool     playing = false;
  volatile uint32_t pos     = 0;
};

#endif // RAWSAMPLEPLAYER_H
