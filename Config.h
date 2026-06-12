#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// Teensy 3.6 Drum Machine — central configuration
//
// Pin assignments mirror pin_map_reference.md (the wiring one-pager).
// If hardware changes, update BOTH this file and that document.
// ============================================================================

#include <stdint.h>

// ---------------------------------------------------------------------------
// Reserved pins (documented here, never assigned below)
//
//   9, 11, 13, 22, 23 : Audio shield I2S  (BCLK, MCLK, RX, TX, LRCLK)
//   18, 19            : I2C SDA/SCL       (SGTL5000 codec + SSD1306 OLED)
//   0, 1              : MIDI (Serial1)    — reserved, no MIDI code yet
//   7                 : Serial3 RX        — keep clear for WS2812Serial
// ---------------------------------------------------------------------------

// --- Core sizes -------------------------------------------------------------
#define NUM_VOICES        4
#define NUM_STEPS        16
#define NUM_SAMPLE_SLOTS 16

// --- Audio ------------------------------------------------------------------
#define AUDIO_MEM_BLOCKS  40      // AudioMemory() size; revisit per-milestone
#define OLED_I2C_ADDR     0x3C    // try 0x3D if init fails
#define I2C_CLOCK_HZ      400000  // set AFTER sgtl5000.enable()

// --- OLED redraw gating ------------------------------------------------------
// display.display() blocks ~23 ms (measured by tests/test_oled). A redraw
// must never start when a sequencer step is due within that window + margin.
#define OLED_REDRAW_GATE_US  30000  // min time-to-next-step to start a redraw
#define OLED_MIN_REDRAW_MS   50     // redraw rate limit
#define OLED_FLASH_MS        900    // flash-message duration

// --- WS2812 LED grid (16 steps x 4 voices) ----------------------------------
// WS2812Serial: DMA-driven, does NOT disable interrupts (NeoPixel would
// glitch audio). Pin 8 = Serial3 TX on Teensy 3.6.
#define LED_PIN           8
#define LED_COLS          NUM_STEPS
#define LED_ROWS          NUM_VOICES
#define LED_COUNT         (LED_COLS * LED_ROWS)
// ONE toggle for the strip type:
//   1 = the 16-LED RGBW bring-up strip currently on the test rig
//   0 = the final 64-LED RGB grid (WS2812_GRB)
// Set to 0 when the real grid is wired. Confirm with tests/test_leds.
#define LED_STRIP_RGBW    1
#if LED_STRIP_RGBW
  #define LED_CONFIG        WS2812_GRBW
  #define LED_BYTES_PER_LED 4
#else
  #define LED_CONFIG        WS2812_GRB
  #define LED_BYTES_PER_LED 3
#endif
// 0 = row-major wiring; 1 = serpentine (odd rows reversed). Set after
// building the grid — tests/test_leds Phase 4 shows which one you have.
#define LED_SERPENTINE    0

// --- Rotary encoders (A, B) --------------------------------------------------
#define ENC1_A 24
#define ENC1_B 25
#define ENC2_A 26
#define ENC2_B 27
#define ENC3_A 28
#define ENC3_B 29
#define ENC4_A 30
#define ENC4_B 31

// --- Button matrix 5x5 (diode per key, drive rows LOW, read cols PULLUP) ----
#define MATRIX_ROWS 5
#define MATRIX_COLS 5

static const uint8_t MATRIX_ROW_PINS[MATRIX_ROWS] = { 2, 3, 4, 5, 6 };
static const uint8_t MATRIX_COL_PINS[MATRIX_COLS] = { 14, 15, 16, 17, 20 };

// Button IDs, laid out row-major to match the physical grid
// (pin_map_reference.md "Button layout in the grid")
enum ButtonId : uint8_t {
  // Row 0
  BTN_KEY_C = 0, BTN_KEY_CS, BTN_KEY_D, BTN_KEY_DS, BTN_KEY_E,
  // Row 1
  BTN_KEY_F, BTN_KEY_FS, BTN_KEY_G, BTN_KEY_GS, BTN_KEY_A,
  // Row 2
  BTN_KEY_AS, BTN_KEY_B, BTN_ACCENT, BTN_TRANSPORT, BTN_MODE,
  // Row 3
  BTN_SHIFT, BTN_OCTAVE, BTN_ENC1, BTN_ENC2, BTN_ENC3,
  // Row 4
  BTN_ENC4, BTN_SPARE1, BTN_SPARE2, BTN_SPARE3, BTN_SPARE4,

  BTN_COUNT
};

// First 12 button IDs are the chromatic keys C..B (semitone = ButtonId)
#define NUM_KEYS 12

static const char* const BUTTON_NAMES[BTN_COUNT] = {
  "C",  "C#", "D",  "D#", "E",
  "F",  "F#", "G",  "G#", "A",
  "A#", "B",  "ACCENT", "TRANSPORT", "MODE",
  "SHIFT", "OCTAVE", "ENC1", "ENC2", "ENC3",
  "ENC4", "SPARE1", "SPARE2", "SPARE3", "SPARE4"
};

// --- Input timing -------------------------------------------------------------
#define MATRIX_SCAN_INTERVAL_MS  1   // full matrix scan rate
#define BUTTON_DEBOUNCE_MS       10  // per-key debounce window
#define BUTTON_HOLD_MS           500 // press this long = "hold" event

// --- Storage -----------------------------------------------------------------
#define SD_CS_PIN         BUILTIN_SDCARD  // Teensy 3.6 onboard SDIO slot
#define EEPROM_MAGIC      0xD2A7          // GlobalState validity marker
#define EEPROM_VERSION    1

// --- Sample RAM pool (recorded + SD-loaded samples) ---------------------------
// Teensy 3.6: 256 KB RAM total. Keep headroom for audio library + app state.
// The pool is partitioned into fixed record slots (never individually freed).
#define SAMPLE_POOL_BYTES   (128 * 1024)
#define NUM_RECORD_SLOTS    4
#define RECORD_SLOT_SAMPLES (SAMPLE_POOL_BYTES / 2 / NUM_RECORD_SLOTS) // ~0.74 s
// Recording decimates the 44.1 kHz stream by 2 (pair-averaged): half the RAM,
// and the ResamplingPlayer's rate correction keeps the pitch right.
#define RECORD_THRESHOLD    900   // |sample| that starts an armed take (~2.7% FS)
#define RECORD_TAIL_LEVEL   450   // trailing-trim floor
#define RECORD_TAIL_PAD     1300  // samples kept past the last loud one (~60 ms)

// --- Feature flags -------------------------------------------------------------
#define FEATURE_REVERB_SEND  0   // enable only if CPU headroom allows (M8)
#define FEATURE_DELAY_SEND   0

#endif // CONFIG_H
