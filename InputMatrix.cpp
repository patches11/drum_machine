#include <Arduino.h>
#include "InputMatrix.h"

InputMatrixClass InputMatrix;

void InputMatrixClass::begin() {
  for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
    pinMode(MATRIX_ROW_PINS[r], INPUT);          // idle rows high-impedance
  }
  for (uint8_t c = 0; c < MATRIX_COLS; c++) {
    pinMode(MATRIX_COL_PINS[c], INPUT_PULLUP);
  }
}

void InputMatrixClass::push(uint8_t id, ButtonEventType type) {
  uint8_t next = (qHead + 1) % QUEUE_SIZE;
  if (next == qTail) return;                     // full: drop newest
  queue[qHead] = { id, type };
  qHead = next;
}

bool InputMatrixClass::nextEvent(ButtonEvent& ev) {
  if (qTail == qHead) return false;
  ev = queue[qTail];
  qTail = (qTail + 1) % QUEUE_SIZE;
  return true;
}

bool InputMatrixClass::isHeld(uint8_t id) const {
  return id < BTN_COUNT && stable[id];
}

void InputMatrixClass::scan() {
  unsigned long now = millis();
  if (now - lastScanMs < MATRIX_SCAN_INTERVAL_MS) return;
  lastScanMs = now;

  for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
    pinMode(MATRIX_ROW_PINS[r], OUTPUT);
    digitalWrite(MATRIX_ROW_PINS[r], LOW);
    delayMicroseconds(5);                        // line settle

    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
      uint8_t id = r * MATRIX_COLS + c;
      bool raw   = (digitalRead(MATRIX_COL_PINS[c]) == LOW);

      if (raw != lastRaw[id]) {
        lastRaw[id]    = raw;
        lastChange[id] = now;
      }
      if (raw != stable[id] && (now - lastChange[id]) >= BUTTON_DEBOUNCE_MS) {
        stable[id] = raw;
        if (raw) {
          pressedAt[id] = now;
          holdSent[id]  = false;
          push(id, BTN_PRESS);
        } else {
          push(id, BTN_RELEASE);
        }
      }
      // hold detection (fires once per press)
      if (stable[id] && !holdSent[id] &&
          (now - pressedAt[id]) >= BUTTON_HOLD_MS) {
        holdSent[id] = true;
        push(id, BTN_HOLD);
      }
    }

    pinMode(MATRIX_ROW_PINS[r], INPUT);          // release the row
  }
}
