# Resampling Sample Player — Detailed Spec (Phase 4)

A custom `AudioStream` source that plays a one-shot sample at an arbitrary playback rate, giving you the octave keyboard and per-voice tuning. One instance per voice. Sits at the head of each voice chain:

```
ResamplingPlayer ─► AudioEffectEnvelope (decay) ─► AudioFilterStateVariable (LP) ─► mixer
```

The player does **only** playback + pitch. Velocity→gain is set on the mixer, decay on the envelope, tone on the filter — all downstream. Keep it focused.

Audio runs in `AUDIO_BLOCK_SAMPLES` (128) blocks at `AUDIO_SAMPLE_RATE_EXACT` — which is **≈ 44117.6 Hz** on Teensy, not exactly 44100. Always use the macro, never a hardcoded 44100, or your tuning drifts slightly sharp/flat.

---

## 1. Sample format & descriptor

Mono 16-bit signed PCM. On Teensy 3.6 (ARM) flash is memory-mapped, so a `PROGMEM` array and a RAM buffer are read the **same way** — a normal pointer dereference. One code path covers flash samples and mic-recorded RAM samples.

```cpp
struct Sample {
    const int16_t* data       = nullptr;  // mono PCM (flash or RAM, both direct-access)
    uint32_t       length     = 0;        // in samples
    uint32_t       sampleRate = 44100;    // source rate; corrects pitch if != audio rate
};
```

`sampleRate` matters: a 22.05 kHz sample played as if it were the audio rate would sound an octave low. The player corrects for this automatically (§4).

---

## 2. Class interface

```cpp
class ResamplingPlayer : public AudioStream {
public:
    ResamplingPlayer() : AudioStream(0, nullptr) {}   // 0 inputs => it's a source

    void play(const Sample& s);        // trigger from the start
    void stop();                       // silence immediately
    void setPlaybackRate(float ratio); // 1.0 = native, 2.0 = +octave, 0.5 = -octave
    bool isPlaying() const { return playing; }

    virtual void update() override;

private:
    void updateRateFixed();

    Sample            sample;
    volatile bool     playing    = false;
    volatile uint64_t pos        = 0;    // 32.32 fixed-point read position
    volatile uint64_t rateFixed  = 0;    // 32.32 fixed-point increment / output sample
    float             pitchRatio = 1.0f;
};
```

---

## 3. Position as fixed-point (not float)

Read position is a **32.32 fixed-point** value in a `uint64_t`: the high 32 bits are the integer sample index, the low 32 bits are the fraction. Accumulating a `float` position over a long sample loses precision; fixed-point doesn't, and the M4F's 32×32→64 multiply makes it cheap.

```
integer index : pos >> 32
fraction      : pos & 0xFFFFFFFF
increment     : pos += rateFixed   (per output sample)
```

---

## 4. Rate computation (pitch × sample-rate correction)

```cpp
void ResamplingPlayer::updateRateFixed() {
    float base = (float)sample.sampleRate / AUDIO_SAMPLE_RATE_EXACT;  // ~44117.6
    double eff = (double)pitchRatio * base;                          // combined rate
    rateFixed  = (uint64_t)(eff * 4294967296.0);                     // * 2^32
}
```

`AudioEngine` computes `pitchRatio` from the played semitones + the voice's tune; the player folds in the sample-rate correction. Default `sampleRate = 44100` so `base ≈ 1.0` when unset.

---

## 5. Trigger / stop / rate change — guard the shared state

These are called from the **control loop**, but the fields are read by `update()` in the **audio interrupt**. `pos`/`rateFixed` are 64-bit (non-atomic on a 32-bit MCU), so wrap multi-field and 64-bit writes in `AudioNoInterrupts()/AudioInterrupts()`.

```cpp
void ResamplingPlayer::play(const Sample& s) {
    AudioNoInterrupts();
    sample = s;
    pos = 0;
    updateRateFixed();
    playing = true;
    AudioInterrupts();
}

void ResamplingPlayer::stop() {
    AudioNoInterrupts();
    playing = false;
    AudioInterrupts();
}

void ResamplingPlayer::setPlaybackRate(float ratio) {  // safe to call mid-note (pitch bend)
    AudioNoInterrupts();
    pitchRatio = ratio;
    updateRateFixed();
    AudioInterrupts();
}
```

Calling `play()` again while sounding resets `pos = 0` → restart from the top. That **is** the desired mono-per-voice retrigger (a fast hat roll re-fires and cuts the previous hit).

---

## 6. `update()` — the interpolation core

Produces one 128-sample block. When idle it transmits **nothing**, which downstream objects read as silence — so no CPU is wasted on silent voices.

```cpp
void ResamplingPlayer::update() {
    if (!playing) return;                       // idle => silence downstream

    audio_block_t* block = allocate();
    if (!block) return;                         // out of audio memory: skip this block

    const int16_t* data = sample.data;
    uint32_t lastIndex  = sample.length - 1;    // need idx+1 for interpolation

    int i = 0;
    for (; i < AUDIO_BLOCK_SAMPLES; i++) {
        uint32_t idx = (uint32_t)(pos >> 32);
        if (idx >= lastIndex) break;            // reached the end

        int32_t  a    = data[idx];
        int32_t  b    = data[idx + 1];
        uint32_t frac = (uint32_t)((pos >> 16) & 0xFFFF);          // 0..65535

        // linear interpolation; int64 product avoids 32-bit overflow
        int32_t s = a + (int32_t)(((int64_t)(b - a) * frac) >> 16);
        block->data[i] = (int16_t)s;

        pos += rateFixed;
    }

    // zero any remaining samples after the end, then stop
    for (; i < AUDIO_BLOCK_SAMPLES; i++) block->data[i] = 0;
    if ((uint32_t)(pos >> 32) >= lastIndex) playing = false;

    transmit(block);
    release(block);
}
```

**Overflow note:** `(b - a)` is up to ±65535 and `frac` up to 65535, so their product can exceed `int32`. The `int64_t` cast on the multiply (one cycle on the M4F via `SMULL`) keeps it correct.

---

## 7. Integration: `AudioEngine::trigger()`

This is the glue that ties together tuning, velocity, choke, and envelope from earlier phases:

```cpp
void AudioEngine::trigger(uint8_t v, uint8_t velocity, int8_t stepPitch) {
    Voice& vc = pattern.voices[v];

    // choke group: cut others sharing the group (e.g., closed hat chokes open hat)
    if (vc.chokeGroup) {
        for (uint8_t o = 0; o < NUM_VOICES; o++)
            if (o != v && pattern.voices[o].chokeGroup == vc.chokeGroup) {
                players[o].stop();
                envelopes[o].noteOff();
            }
    }

    // pitch ratio = played semitones (key/step) + per-voice tune
    float semis = (float)stepPitch + (float)vc.rootSemis;
    float ratio = powf(2.0f, semis / 12.0f) * powf(2.0f, vc.fineCents / 1200.0f);
    players[v].setPlaybackRate(ratio);

    // velocity -> mixer gain, with a perceptual (squared) curve and per-voice level
    float gv = velocity / 127.0f; gv *= gv;
    mixer.gain(mixerChannelFor(v), gv * (vc.level / 127.0f));

    // fire
    players[v].play(samples[vc.sampleId]);
    envelopes[v].noteOn();   // envelope release time derived from vc.decay
}
```

---

## 8. Notes, limits, refinements

- **CPU is cheap.** A handful of cycles per output sample → roughly ~1% of the 180 MHz M4F for all four voices. Interpolation is not your bottleneck; effects (Freeverb) are.
- **Aliasing on pitch-up.** Reading faster than 1.0 with no decimation filter can alias on bright material. Fine across ~one octave; audible only at extreme up-pitch. Mipmapped samples or oversample-and-filter would fix it but are overkill for drums.
- **Declick.** Cutting a sample mid-playback (retrigger or choke) can click. If you hear it on fast rolls, add a short (~32–64 sample) linear fade-out on `stop()`/retrigger and fade-in on `play()`, or let the following `AudioEffectEnvelope` do a fast attack/release.
- **Concurrency.** All state shared with the ISR is guarded with `AudioNoInterrupts()`. Never `allocate()`/`release()` outside `update()`, and keep `powf()` etc. in the control loop (as in `trigger()`), not in the audio path.
- **Optional looping.** For sustained (non-drum) samples, add `loopStart`/`loopEnd` and wrap `pos` instead of stopping. Not needed for one-shot drums.
- `AUDIO_BLOCK_SAMPLES` and `AUDIO_SAMPLE_RATE_EXACT` are Teensy Audio Library defines.

---

## 9. Test plan for this phase

1. **Native (ratio 1.0):** output matches the source; duration correct; stops cleanly at the end with `isPlaying()` going false.
2. **Octave up/down (2.0 / 0.5):** duration halves/doubles, pitch shifts exactly an octave (check against a tuner with a tonal sample).
3. **12-key sweep:** the chromatic keys land on equal-tempered semitone steps.
4. **Sample-rate correction:** a 22.05 kHz sample plays at correct pitch, not an octave low.
5. **Bounds:** confirm no read past `length` (the `idx + 1` guard); fuzz with tiny and 1-sample buffers.
6. **Retrigger:** rapid `play()` restarts from 0 without artifacts; verify declick if added.
7. **Choke:** triggering a choke-group partner cuts the other voice.
8. **Budget:** four voices at high rates stay within CPU and `AudioMemory()`; no `allocate()` failures.
