#ifndef LEDS_H
#define LEDS_H

// Leds — renders the 16x4 WS2812 grid via WS2812Serial (DMA, interrupt-safe;
// never Adafruit_NeoPixel, which disables interrupts and glitches audio).
//
// Grid semantics (plan §5):
//   * row = voice, column = step
//   * active step: voice color, brightness = velocity
//   * accent: brighter / whiter
//   * playhead column: white overlay
//   * edit cursor: bright white on the selected row at the cursor column
//   * selected-voice row: faint voice-color tint on empty steps

#include <stdint.h>
#include "Config.h"

class LedsClass {
public:
  void begin();

  // Redraw the whole grid from AppState + playhead. Cheap (~64 pixel
  // writes + DMA kick); call when dirty, not every loop.
  void render(uint8_t playheadStep, bool transportRunning);

private:
  int index(uint8_t row, uint8_t col) const;
};

extern LedsClass Leds;

#endif // LEDS_H
