// ============================================================================
// test_sd — Teensy 3.6 built-in SDIO slot bring-up
//
// Hardware needed: microSD card in the Teensy's ONBOARD slot
//                  (not the audio shield's slot — those pins stay free).
//
// Pass criteria:
//   * Card detected via BUILTIN_SDCARD
//   * Root directory listing prints
//   * 200 KB write + read complete, with throughput numbers
//     (read speed matters for kit loading at boot; writes for saves)
// ============================================================================

#include <SD.h>
#include "../../Config.h"

#define TEST_FILE  "sdtest.bin"
#define TEST_BYTES (200 * 1024)

void listRoot() {
  Serial.println("Root directory:");
  File root = SD.open("/");
  int count = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    Serial.print("  ");
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
    } else {
      Serial.print("  ");
      Serial.print(entry.size());
      Serial.println(" bytes");
    }
    entry.close();
    count++;
  }
  root.close();
  if (count == 0) Serial.println("  (empty)");
}

void speedTest() {
  static uint8_t buf[4096];
  for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)i;

  // --- write ---
  SD.remove(TEST_FILE);
  File f = SD.open(TEST_FILE, FILE_WRITE);
  if (!f) { Serial.println("write open FAILED"); return; }
  unsigned long t0 = millis();
  for (int written = 0; written < TEST_BYTES; written += sizeof(buf)) {
    f.write(buf, sizeof(buf));
  }
  f.close();
  unsigned long wMs = millis() - t0;

  // --- read ---
  f = SD.open(TEST_FILE);
  if (!f) { Serial.println("read open FAILED"); return; }
  t0 = millis();
  int total = 0;
  while (f.available()) total += f.read(buf, sizeof(buf));
  f.close();
  unsigned long rMs = millis() - t0;

  SD.remove(TEST_FILE);

  Serial.print("write: 200 KB in "); Serial.print(wMs);
  Serial.print(" ms ("); Serial.print(200.0f / wMs * 1000.0f, 0); Serial.println(" KB/s)");
  Serial.print("read:  "); Serial.print(total / 1024);
  Serial.print(" KB in "); Serial.print(rMs);
  Serial.print(" ms ("); Serial.print((total / 1024.0f) / rMs * 1000.0f, 0); Serial.println(" KB/s)");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("=== test_sd ===");

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("Card NOT detected in built-in slot.");
    Serial.println("Check: card seated? FAT32/exFAT formatted?");
    while (1) {}
  }
  Serial.println("Card detected (BUILTIN_SDCARD).");

  listRoot();
  speedTest();
  Serial.println("Done. Send any character to re-run the speed test.");
}

void loop() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    speedTest();
  }
}
