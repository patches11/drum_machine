// ============================================================================
// test_oled — SSD1306 on the shared I2C bus (with the SGTL5000 codec)
//
// Hardware needed: audio shield stacked, OLED on pins 18/19.
//
// Pass criteria:
//   * Display initializes and renders a live counter (codec + OLED coexist
//     on the same bus at 400 kHz)
//   * Serial reports the measured display.display() transfer time.
//     EXPECT roughly 20-25 ms — this number feeds the production redraw
//     gate (a redraw must never start when a sequencer event is due
//     within that window + margin).
//
// If init fails, try OLED_I2C_ADDR 0x3D in Config.h.
// Open Serial Monitor at 115200.
// ============================================================================

#include <Audio.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../../Config.h"

AudioControlSGTL5000 sgtl5000;     // init the codec first — proves bus sharing
Adafruit_SSD1306 display(128, 64, &Wire, -1);

unsigned long worstUs = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println("=== test_oled ===");

  // Same init order as production: codec first, then bump the bus clock.
  sgtl5000.enable();
  Wire.setClock(I2C_CLOCK_HZ);
  delay(500);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("SSD1306 init FAILED — try address 0x3D in Config.h");
    while (1) {}
  }
  Serial.println("SSD1306 OK on shared bus.");
}

void loop() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("test_oled");
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 18);
  display.print(millis() / 1000);
  display.print("s");

  display.setTextSize(1);
  display.setCursor(0, 44);
  display.print("xfer: ");
  display.print(worstUs / 1000.0f, 1);
  display.print(" ms worst");

  unsigned long t0 = micros();
  display.display();                       // the I2C transfer being measured
  unsigned long dt = micros() - t0;
  if (dt > worstUs) worstUs = dt;

  static unsigned long lastReport = 0;
  if (millis() - lastReport >= 2000) {
    lastReport = millis();
    Serial.print("display.display(): last ");
    Serial.print(dt / 1000.0f, 2);
    Serial.print(" ms, worst ");
    Serial.print(worstUs / 1000.0f, 2);
    Serial.println(" ms  <-- production redraw gate must exceed this");
  }

  delay(100);
}
