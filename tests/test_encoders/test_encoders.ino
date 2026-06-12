// ============================================================================
// test_encoders — 4x rotary encoder bring-up
//
// Hardware needed: encoders on pins 24/25, 26/27, 28/29, 30/31
//                  (push switches are in the button matrix — see test_matrix)
//
// Pass criteria:
//   * Turning each encoder one detent prints a consistent delta
//     (typically +/-4 counts per detent for quadrature encoders;
//      the production code will divide by 4)
//   * No spurious counts when the encoder is untouched (jitter)
//   * Direction is consistent: clockwise = positive on all four
//     (if one is reversed, swap its A/B pins in Config.h)
//
// Open Serial Monitor at 115200.
// ============================================================================

#include <Encoder.h>
#include "../../Config.h"

Encoder enc1(ENC1_A, ENC1_B);
Encoder enc2(ENC2_A, ENC2_B);
Encoder enc3(ENC3_A, ENC3_B);
Encoder enc4(ENC4_A, ENC4_B);

long last[4] = {0, 0, 0, 0};

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("=== test_encoders ===");
  Serial.println("Turn each encoder; deltas and totals print below.");
}

void report(int idx, long val) {
  long delta = val - last[idx];
  if (delta == 0) return;
  last[idx] = val;
  Serial.print("Enc ");
  Serial.print(idx + 1);
  Serial.print("  delta ");
  Serial.print(delta > 0 ? "+" : "");
  Serial.print(delta);
  Serial.print("  total ");
  Serial.print(val);
  Serial.print("  detents ");
  Serial.println(val / 4);
}

void loop() {
  report(0, enc1.read());
  report(1, enc2.read());
  report(2, enc3.read());
  report(3, enc4.read());
  delay(5);
}
