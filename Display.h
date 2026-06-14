#ifndef DISPLAY_H
#define DISPLAY_H

// Display — SSD1306 modeless 4-column UI (plan §5 "UI / Display").
//
// Layout (128x64):
//   y 0..9   header: mode name | [C]hain + pattern + play arrow + BPM (right)
//   y 16..   four 32-px columns, one per encoder: label / value / bar
//   y 56..   status line: selected voice (+ M/S flags) + octave shift
//   flash(): ~1 s inverted message box (mode/voice/sample changes)
//
// Labels and values come from Controls' binding table — the same table
// that dispatches the encoders — so what's on screen IS what the knobs do.
//
// Redraw discipline (the M5 gate):
//   * display.display() blocks ~23 ms on the shared I2C bus
//   * update() only starts a transfer when (a) something is dirty,
//     (b) at least OLED_MIN_REDRAW_MS since the last one, and
//     (c) no sequencer step is due within OLED_REDRAW_GATE_US
//   * begin() must run AFTER AudioEngine.begin() (codec owns I2C init order)
//   * if the OLED is absent/unwired the module goes inert — never hangs

#include <stdint.h>
#include "Config.h"

class DisplayClass {
public:
  void begin();                 // call after AudioEngine.begin()
  void update();                // call every loop; draws when safe
  void markDirty() { dirty = true; }
  void flash(const char* msg);  // ~1 s overlay message

  bool present() const { return ok; }

private:
  void draw();

  bool          ok        = false;
  bool          dirty     = true;
  unsigned long lastDrawMs = 0;
  unsigned long flashUntil = 0;
  char          flashMsg[20] = "";
};

extern DisplayClass Display;

#endif // DISPLAY_H
