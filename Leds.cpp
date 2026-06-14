#include <WS2812Serial.h>
#include "Leds.h"
#include "AppState.h"

LedsClass Leds;

static byte drawingMemory[LED_COUNT * LED_BYTES_PER_LED];
DMAMEM static byte displayMemory[LED_COUNT * LED_BYTES_PER_LED * 4];
static WS2812Serial strip(LED_COUNT, displayMemory, drawingMemory,
                          LED_PIN, LED_CONFIG);

// Per-voice base colors (row 0..3 = kick/snare/hat/clap)
static const uint8_t VOICE_COLOR[NUM_VOICES][3] = {
  { 255,   0,   0 },   // kick  - red
  { 255, 140,   0 },   // snare - amber
  {   0, 255,   0 },   // hat   - green
  {   0,  80, 255 },   // clap  - blue
};

// Global brightness cap (the grid does not need to sear eyeballs;
// also keeps worst-case current well under the supply limit)
static const uint8_t MASTER_BRIGHTNESS = 60;   // out of 255

int LedsClass::index(uint8_t row, uint8_t col) const {
#if LED_STRIP_RGBW
  // 16-LED bring-up strip: only one row fits, so show the SELECTED
  // voice's row (press 'v' / turn the voice encoder to inspect others).
  return (row == editState.voice) ? col : -1;
#else
  #if LED_SERPENTINE
  if (row & 1) col = LED_COLS - 1 - col;
  #endif
  return row * LED_COLS + col;
#endif
}

void LedsClass::begin() {
  strip.begin();
  for (int i = 0; i < LED_COUNT; i++) strip.setPixel(i, 0, 0, 0);
  strip.show();
}

static inline uint8_t scale(uint8_t c, uint16_t num, uint16_t den) {
  return (uint8_t)((uint32_t)c * num * MASTER_BRIGHTNESS / (den * 255));
}

void LedsClass::render(uint8_t playheadStep, bool transportRunning) {
  // M10 mute/solo: a silenced row (muted, or unsoloed while another voice
  // solos) shows its hits ghost-dim — you can see the pattern is still
  // there, and that it isn't sounding.
  bool anySolo = false;
  for (uint8_t v = 0; v < NUM_VOICES; v++)
    if (pattern.voices[v].solo) { anySolo = true; break; }

  for (uint8_t row = 0; row < LED_ROWS; row++) {
    const Voice& vc = pattern.voices[row];
    bool selectedRow = (row == editState.voice);
    bool silenced    = vc.mute || (anySolo && !vc.solo);
    for (uint8_t col = 0; col < LED_COLS; col++) {
      const Step& st = vc.steps[col];
      uint8_t r = 0, g = 0, b = 0;

      if (st.active) {
        // brightness = velocity (plan §5); accent pushes toward white
        uint16_t vel = st.velocity;
        if (vel < 24) vel = 24;                       // keep dim hits visible
        if (silenced) vel = 16;                       // ghost-dim, no accent
        r = scale(VOICE_COLOR[row][0], vel, 127);
        g = scale(VOICE_COLOR[row][1], vel, 127);
        b = scale(VOICE_COLOR[row][2], vel, 127);
        if (st.accent && !silenced) {
          r = (uint8_t)min(255, r + 40);
          g = (uint8_t)min(255, g + 40);
          b = (uint8_t)min(255, b + 40);
        }
      } else if (selectedRow) {
        // selected-voice row: faint voice-color tint on empty steps
        r = scale(VOICE_COLOR[row][0], 12, 127);
        g = scale(VOICE_COLOR[row][1], 12, 127);
        b = scale(VOICE_COLOR[row][2], 12, 127);
      } else if (col % 4 == 0) {
        // faint beat markers on empty quarter-note columns
        r = g = b = 2;
      }

      // edit cursor: bright white on the selected row at the cursor column
      if (selectedRow && col == editState.cursor) {
        r = (uint8_t)min(255, r + MASTER_BRIGHTNESS + 30);
        g = (uint8_t)min(255, g + MASTER_BRIGHTNESS + 30);
        b = (uint8_t)min(255, b + MASTER_BRIGHTNESS + 30);
      }

      // playhead column: white overlay while running
      if (transportRunning && col == playheadStep) {
        r = (uint8_t)min(255, r + MASTER_BRIGHTNESS);
        g = (uint8_t)min(255, g + MASTER_BRIGHTNESS);
        b = (uint8_t)min(255, b + MASTER_BRIGHTNESS);
      }

      int ix = this->index(row, col);
      if (ix >= 0) strip.setPixel(ix, r, g, b);
    }
  }
  strip.show();   // DMA — returns immediately, audio unaffected
}
