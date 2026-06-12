// ============================================================================
// test_leds — WS2812Serial 64-LED grid bring-up (DMA, interrupt-safe)
//
// Hardware needed: LED chain data on pin 8 (Serial3 TX), DEDICATED 5V supply,
//                  common ground with the Teensy. Audio shield stacked
//                  (the audio-glitch test needs it).
//
// Pass criteria:
//   1. COLOR ORDER: first three LEDs show RED, GREEN, BLUE (in that order).
//      If not, the chip order differs — adjust LED_CONFIG in Config.h.
//      (RGBW strip? Set LED_CONFIG WS2812_GRBW + LED_BYTES_PER_LED 4.)
//   2. WALKING PIXEL: a single white pixel walks the whole chain — confirms
//      all 64 LEDs pass data.
//   3. POWER: full grid at 25% white — no brownout, no flicker, no color
//      shift at the far end (voltage drop check).
//   4. AUDIO: a sine tone plays the entire time — listen for ANY glitching
//      while LEDs refresh. WS2812Serial uses DMA so there should be none.
//
// Open Serial Monitor at 115200 to see phase announcements.
// ============================================================================

#include <Audio.h>
#include <Wire.h>
#include <WS2812Serial.h>
#include "../../Config.h"

// --- audio: steady sine so LED-induced glitches are audible -----------------
AudioSynthWaveform   sine;
AudioOutputI2S       i2sOut;
AudioControlSGTL5000 sgtl5000;
AudioConnection      pc1(sine, 0, i2sOut, 0);
AudioConnection      pc2(sine, 0, i2sOut, 1);

// --- LEDs --------------------------------------------------------------------
byte drawingMemory[LED_COUNT * LED_BYTES_PER_LED];
DMAMEM byte displayMemory[LED_COUNT * LED_BYTES_PER_LED * 4];
WS2812Serial leds(LED_COUNT, displayMemory, drawingMemory, LED_PIN, LED_CONFIG);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("=== test_leds ===");

  AudioMemory(8);
  sgtl5000.enable();
  sgtl5000.volume(0.5f);
  sgtl5000.lineOutLevel(21);
  sine.begin(WAVEFORM_SINE);
  sine.frequency(440.0f);
  sine.amplitude(0.3f);
  Serial.println("440 Hz sine playing — listen for glitches throughout.");

  leds.begin();
}

void allOff() {
  for (int i = 0; i < LED_COUNT; i++) leds.setPixel(i, 0, 0, 0);
}

void loop() {
  // --- Phase 1: color order ---
  Serial.println("Phase 1: color order — LED0=RED LED1=GREEN LED2=BLUE (5 s)");
  allOff();
  leds.setPixel(0, 128, 0, 0);
  leds.setPixel(1, 0, 128, 0);
  leds.setPixel(2, 0, 0, 128);
  leds.show();
  delay(5000);

  // --- Phase 2: walking pixel ---
  Serial.println("Phase 2: walking pixel — all 64 positions");
  for (int i = 0; i < LED_COUNT; i++) {
    allOff();
    leds.setPixel(i, 96, 96, 96);
    leds.show();
    delay(60);
  }

  // --- Phase 3: full-grid power test ---
  Serial.println("Phase 3: full grid 25% white (5 s) — watch for brownout/flicker");
  for (int i = 0; i < LED_COUNT; i++) leds.setPixel(i, 64, 64, 64);
  leds.show();
  delay(5000);

  // --- Phase 4: row/column sanity (grid mapping preview) ---
  Serial.println("Phase 4: voice rows — R0 red, R1 green, R2 blue, R3 yellow (5 s)");
  for (int row = 0; row < LED_ROWS; row++) {
    for (int col = 0; col < LED_COLS; col++) {
      int i = row * LED_COLS + col;   // straight row-major; serpentine? note it!
      switch (row) {
        case 0: leds.setPixel(i, 96,  0,  0); break;
        case 1: leds.setPixel(i,  0, 96,  0); break;
        case 2: leds.setPixel(i,  0,  0, 96); break;
        case 3: leds.setPixel(i, 96, 72,  0); break;
      }
    }
  }
  leds.show();
  delay(5000);

  Serial.println("Cycle complete; repeating.\n");
}
