// ============================================================================
// test_matrix — 5x5 diode button-matrix bring-up
//
// Hardware needed: button matrix on rows 2-6 (driven), cols 14-17,20 (read),
//                  one diode per key, cathode toward the row line.
//
// Pass criteria:
//   * Serial prints the button NAME on every press and release
//   * Hold 3 note keys + SHIFT simultaneously -> all four report held,
//     and NO other (ghost) buttons appear. If ghosts appear, a diode is
//     missing or reversed.
//
// Open Serial Monitor at 115200.
// ============================================================================

#include "../../Config.h"

bool stable[BTN_COUNT];                 // debounced state, true = pressed
bool lastRaw[BTN_COUNT];
unsigned long lastChange[BTN_COUNT];

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
    pinMode(MATRIX_ROW_PINS[r], INPUT);     // idle rows float (released)
  }
  for (uint8_t c = 0; c < MATRIX_COLS; c++) {
    pinMode(MATRIX_COL_PINS[c], INPUT_PULLUP);
  }

  Serial.println("=== test_matrix ===");
  Serial.println("Press buttons; names print on press/release.");
  Serial.println("Ghost test: hold 3 keys + SHIFT, watch for phantom names.");
}

void printHeld() {
  Serial.print("  held: [");
  bool first = true;
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    if (stable[i]) {
      if (!first) Serial.print(", ");
      Serial.print(BUTTON_NAMES[i]);
      first = false;
    }
  }
  Serial.println("]");
}

void loop() {
  unsigned long now = millis();

  for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
    // Drive one row LOW, leave the others high-impedance
    pinMode(MATRIX_ROW_PINS[r], OUTPUT);
    digitalWrite(MATRIX_ROW_PINS[r], LOW);
    delayMicroseconds(10);                   // settle

    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
      uint8_t id  = r * MATRIX_COLS + c;
      bool raw    = (digitalRead(MATRIX_COL_PINS[c]) == LOW);  // LOW = pressed

      if (raw != lastRaw[id]) {
        lastRaw[id]    = raw;
        lastChange[id] = now;
      }
      if (raw != stable[id] && (now - lastChange[id]) >= BUTTON_DEBOUNCE_MS) {
        stable[id] = raw;
        Serial.print(raw ? "PRESS   " : "RELEASE ");
        Serial.println(BUTTON_NAMES[id]);
        printHeld();
      }
    }

    pinMode(MATRIX_ROW_PINS[r], INPUT);      // release the row
  }

  delay(MATRIX_SCAN_INTERVAL_MS);
}
