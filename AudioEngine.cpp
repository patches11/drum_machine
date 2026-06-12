#include <Audio.h>
#include <Wire.h>
#include "AudioEngine.h"
#include "ResamplingPlayer.h"
#include "SampleStore.h"

AudioEngineClass AudioEngine;

// ---------------------------------------------------------------------------
// Graph objects (static — Teensy Audio objects must outlive everything)

static ResamplingPlayer         players[NUM_VOICES];
static AudioEffectEnvelope      envelopes[NUM_VOICES];
static AudioFilterStateVariable filters[NUM_VOICES];
static AudioMixer4              mixer;
static AudioOutputI2S           i2sOut;
static AudioControlSGTL5000     sgtl5000;

static AudioConnection pV0a(players[0], 0, envelopes[0], 0);
static AudioConnection pV1a(players[1], 0, envelopes[1], 0);
static AudioConnection pV2a(players[2], 0, envelopes[2], 0);
static AudioConnection pV3a(players[3], 0, envelopes[3], 0);
static AudioConnection pV0b(envelopes[0], 0, filters[0], 0);
static AudioConnection pV1b(envelopes[1], 0, filters[1], 0);
static AudioConnection pV2b(envelopes[2], 0, filters[2], 0);
static AudioConnection pV3b(envelopes[3], 0, filters[3], 0);
// filter output 0 = lowpass
static AudioConnection pV0c(filters[0], 0, mixer, 0);
static AudioConnection pV1c(filters[1], 0, mixer, 1);
static AudioConnection pV2c(filters[2], 0, mixer, 2);
static AudioConnection pV3c(filters[3], 0, mixer, 3);
// mono: same mix to both channels (plan §4 — single speaker, lose nothing)
static AudioConnection pOutL(mixer, 0, i2sOut, 0);
static AudioConnection pOutR(mixer, 0, i2sOut, 1);

// Per-channel headroom: 4 voices summing at full scale must not clip.
static const float VOICE_GAIN = 0.55f;

// Which sample slot each voice is currently sounding (for stopVoicesUsing)
static uint8_t playingSlot[NUM_VOICES] = {255, 255, 255, 255};

// ---------------------------------------------------------------------------

static float decayToMs(uint8_t decay) {        // 0..127 -> 15..780 ms
  return 15.0f + (float)decay * 6.0f;
}

static float cutToHz(uint8_t cut) {            // 0..127 -> ~40 Hz..~10 kHz
  return 40.0f * powf(2.0f, (float)cut / 16.0f);
}

void AudioEngineClass::begin() {
  AudioMemory(AUDIO_MEM_BLOCKS);

  sgtl5000.enable();
  Wire.setClock(I2C_CLOCK_HZ);   // after enable() — proven on the test rig
  setVolume(globalState.volume);
  sgtl5000.inputSelect(AUDIO_INPUT_MIC);     // M7: Sampler records the mic
  sgtl5000.micGain(globalState.micGain);
  delay(500);

  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    envelopes[v].delay(0.0f);
    envelopes[v].attack(1.0f);
    envelopes[v].hold(0.0f);
    envelopes[v].sustain(0.0f);
    envelopes[v].release(30.0f);
    filters[v].resonance(0.7f);
    applyVoiceParams(v);
    mixer.gain(v, 0.0f);         // silent until first trigger sets velocity
  }
}

void AudioEngineClass::applyVoiceParams(uint8_t v) {
  if (v >= NUM_VOICES) return;
  const Voice& vc = pattern.voices[v];
  envelopes[v].decay(decayToMs(vc.decay));
  filters[v].frequency(cutToHz(vc.filterCut));
}

void AudioEngineClass::trigger(uint8_t v, uint8_t velocity, int8_t stepPitch) {
  if (v >= NUM_VOICES) return;
  Voice& vc = pattern.voices[v];

  // choke group: cut others sharing the group (resampling spec §7)
  if (vc.chokeGroup) {
    for (uint8_t o = 0; o < NUM_VOICES; o++) {
      if (o != v && pattern.voices[o].chokeGroup == vc.chokeGroup) {
        players[o].stop();
        envelopes[o].noteOff();
      }
    }
  }

  // pitch ratio = played semitones (key/step) + per-voice tune (spec §7).
  // powf stays here in the control loop, never in the audio ISR.
  float semis = (float)stepPitch + (float)vc.rootSemis;
  float ratio = powf(2.0f, semis / 12.0f)
              * powf(2.0f, (float)vc.fineCents / 1200.0f);
  players[v].setPlaybackRate(ratio);

  // velocity -> mixer gain: perceptual (squared) curve x per-voice level
  float gv = (float)velocity / 127.0f;
  gv *= gv;
  mixer.gain(v, gv * ((float)vc.level / 127.0f) * VOICE_GAIN);

  players[v].play(SampleStore.get(vc.sampleId));
  playingSlot[v] = vc.sampleId;
  envelopes[v].noteOn();
}

void AudioEngineClass::stopVoicesUsing(uint8_t sampleSlot) {
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    if (playingSlot[v] == sampleSlot && players[v].isPlaying()) {
      players[v].stop();
      envelopes[v].noteOff();
    }
  }
}

void AudioEngineClass::setVolume(uint8_t vol) {
  if (vol < 1) vol = 1;
  if (vol > 10) vol = 10;
  globalState.volume = vol;
  // Headphone: 0.1..1.0. Line out (PAM8302): 31 (quietest) .. 13 (loudest).
  sgtl5000.volume((float)vol * 0.1f);
  sgtl5000.lineOutLevel(31 - (vol - 1) * 2);
}

void AudioEngineClass::setMicGain(uint8_t gainDb) {
  if (gainDb > 63) gainDb = 63;
  globalState.micGain = gainDb;
  sgtl5000.micGain(gainDb);
}

void AudioEngineClass::printUsage() {
  Serial.print("CPU: ");
  Serial.print(AudioProcessorUsage(), 1);
  Serial.print("% (max ");
  Serial.print(AudioProcessorUsageMax(), 1);
  Serial.print("%)  AudioMemory: ");
  Serial.print(AudioMemoryUsage());
  Serial.print(" (max ");
  Serial.print(AudioMemoryUsageMax());
  Serial.print(" of ");
  Serial.print(AUDIO_MEM_BLOCKS);
  Serial.println(")");
}

void AudioEngineClass::resetUsageMax() {
  AudioProcessorUsageMaxReset();
  AudioMemoryUsageMaxReset();
}
