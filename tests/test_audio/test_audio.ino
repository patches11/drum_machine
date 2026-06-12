// ============================================================================
// test_audio — audio shield bring-up: passthrough, sine, sample trigger
//
// Hardware needed: Teensy + audio shield + amp (PAM8302 on line out).
//
// Serial commands (115200):
//   p : toggle line-in passthrough
//   s : 200 ms sine beep
//   1 : trigger the built-in test sample (synthesized kick, played through
//       AudioPlayMemory — validates the sample-trigger path end to end)
//   u : print CPU / AudioMemory usage   <-- budget telemetry baseline
//   r : reset usage maximums
//
// Pass criteria:
//   * passthrough is clean, beep is clean, sample triggers instantly
//   * 'u' reports sensible numbers (record them — they're the baseline)
// ============================================================================

#include <Audio.h>
#include <Wire.h>
#include "../../Config.h"

AudioInputI2S        lineIn;
AudioSynthWaveform   sine;
AudioPlayMemory      samplePlayer;
AudioMixer4          mixL;
AudioMixer4          mixR;
AudioOutputI2S       i2sOut;
AudioControlSGTL5000 sgtl5000;

AudioConnection c1(lineIn,       0, mixL, 0);
AudioConnection c2(lineIn,       1, mixR, 0);
AudioConnection c3(sine,         0, mixL, 1);
AudioConnection c4(sine,         0, mixR, 1);
AudioConnection c5(samplePlayer, 0, mixL, 2);
AudioConnection c6(samplePlayer, 0, mixR, 2);
AudioConnection c7(mixL,         0, i2sOut, 0);
AudioConnection c8(mixR,         0, i2sOut, 1);

bool passthrough = false;

// --- synthesized test sample in AudioPlayMemory format ----------------------
// Header word: (format 0x81 = 16-bit PCM @ 44100) << 24 | length in samples.
// Data: int16 pairs packed little-endian into uint32s.
#define TEST_SAMPLE_LEN 8820   // 200 ms @ 44.1 kHz
DMAMEM static uint32_t testSample[1 + (TEST_SAMPLE_LEN + 1) / 2];

void buildTestSample() {
  testSample[0] = 0x81000000 | TEST_SAMPLE_LEN;
  int16_t* pcm = (int16_t*)&testSample[1];
  for (int i = 0; i < TEST_SAMPLE_LEN; i++) {
    float t    = i / 44100.0f;
    float freq = 50.0f + 150.0f * expf(-t / 0.03f);   // kick-style pitch sweep
    float amp  = expf(-t / 0.08f);
    pcm[i] = (int16_t)(sinf(2.0f * PI * freq * t) * amp * 28000.0f);
  }
  if (TEST_SAMPLE_LEN & 1) pcm[TEST_SAMPLE_LEN] = 0;  // pad
}

void printUsage() {
  Serial.print("CPU: ");
  Serial.print(AudioProcessorUsage(), 1);
  Serial.print("% (max ");
  Serial.print(AudioProcessorUsageMax(), 1);
  Serial.print("%)   AudioMemory: ");
  Serial.print(AudioMemoryUsage());
  Serial.print(" (max ");
  Serial.print(AudioMemoryUsageMax());
  Serial.print(" of ");
  Serial.print(AUDIO_MEM_BLOCKS);
  Serial.println(")");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("=== test_audio ===");

  AudioMemory(AUDIO_MEM_BLOCKS);
  sgtl5000.enable();
  Wire.setClock(I2C_CLOCK_HZ);
  sgtl5000.volume(0.6f);
  sgtl5000.lineOutLevel(21);
  sgtl5000.inputSelect(AUDIO_INPUT_LINEIN);
  delay(500);

  sine.begin(WAVEFORM_SINE);
  sine.frequency(440.0f);
  sine.amplitude(0.0f);

  // passthrough starts muted
  mixL.gain(0, 0.0f); mixR.gain(0, 0.0f);
  mixL.gain(1, 0.5f); mixR.gain(1, 0.5f);
  mixL.gain(2, 0.8f); mixR.gain(2, 0.8f);
  mixL.gain(3, 0.0f); mixR.gain(3, 0.0f);

  buildTestSample();
  Serial.println("Commands: p=passthrough  s=beep  1=sample  u=usage  r=reset max");
}

void loop() {
  // end the beep
  static unsigned long beepEnd = 0;
  if (beepEnd && millis() >= beepEnd) {
    sine.amplitude(0.0f);
    beepEnd = 0;
  }

  if (!Serial.available()) return;
  char cmd = Serial.read();
  switch (cmd) {
    case 'p':
      passthrough = !passthrough;
      mixL.gain(0, passthrough ? 0.8f : 0.0f);
      mixR.gain(0, passthrough ? 0.8f : 0.0f);
      Serial.println(passthrough ? "passthrough ON" : "passthrough OFF");
      break;
    case 's':
      sine.amplitude(0.5f);
      beepEnd = millis() + 200;
      Serial.println("beep");
      break;
    case '1':
      samplePlayer.play((const unsigned int*)testSample);
      Serial.println("sample trigger");
      break;
    case 'u':
      printUsage();
      break;
    case 'r':
      AudioProcessorUsageMaxReset();
      AudioMemoryUsageMaxReset();
      Serial.println("max counters reset");
      break;
  }
}
