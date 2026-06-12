#ifndef INPUTMATRIX_H
#define INPUTMATRIX_H

// InputMatrix — 5x5 diode button matrix scanner.
//
// Drive one row LOW at a time, read columns (INPUT_PULLUP, diode per key
// = full N-key rollover; see pin_map_reference.md). Debounced per key.
// Emits PRESS / RELEASE / HOLD events into a small ring buffer that the
// Controls layer drains each loop. isHeld() supports chord logic
// (Shift+key, Accent-held-on-entry).

#include <stdint.h>
#include "Config.h"

enum ButtonEventType : uint8_t { BTN_PRESS, BTN_RELEASE, BTN_HOLD };

struct ButtonEvent {
  uint8_t         id;     // ButtonId (Config.h)
  ButtonEventType type;
};

class InputMatrixClass {
public:
  void begin();

  // Scan one full matrix pass. Rate-limited internally; call every loop.
  void scan();

  // Drain events. Returns false when the queue is empty.
  bool nextEvent(ButtonEvent& ev);

  // Current debounced state (chords, modifiers).
  bool isHeld(uint8_t id) const;

private:
  void push(uint8_t id, ButtonEventType type);

  bool          stable[BTN_COUNT]    = {false};
  bool          lastRaw[BTN_COUNT]   = {false};
  unsigned long lastChange[BTN_COUNT] = {0};
  unsigned long pressedAt[BTN_COUNT]  = {0};
  bool          holdSent[BTN_COUNT]   = {false};
  unsigned long lastScanMs           = 0;

  static const uint8_t QUEUE_SIZE = 16;   // power of two
  ButtonEvent queue[QUEUE_SIZE];
  uint8_t     qHead = 0, qTail = 0;
};

extern InputMatrixClass InputMatrix;

#endif // INPUTMATRIX_H
