#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Display.h"
#include "AppState.h"
#include "Controls.h"
#include "Sequencer.h"
#include "SampleStore.h"
#include "Sampler.h"

DisplayClass Display;

static Adafruit_SSD1306 oled(128, 64, &Wire, -1);

static const char* const MODE_TITLES[APP_MODE_COUNT] = {
  "HOME", "PATTERN", "SOUND", "TEMPO", "FEEL", "RECORD",
};

void DisplayClass::begin() {
  // codec already enabled + bus at 400 kHz (AudioEngine.begin ran first)
  ok = oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
  if (!ok) {
    Serial.println("OLED not found (0x3C) - running headless");
    return;
  }
  oled.clearDisplay();
  oled.display();
  dirty = true;
}

void DisplayClass::flash(const char* msg) {
  if (!ok) return;
  strlcpy(flashMsg, msg, sizeof(flashMsg));
  flashUntil = millis() + OLED_FLASH_MS;
  dirty = true;
}

void DisplayClass::update() {
  if (!ok) return;

  unsigned long now = millis();
  if (flashUntil && (long)(now - flashUntil) >= 0) {
    flashUntil = 0;
    dirty = true;                       // redraw to clear the overlay
  }
  if (!dirty) return;
  if (now - lastDrawMs < OLED_MIN_REDRAW_MS) return;

  // THE gate: never start a ~23 ms blocking transfer with a step imminent.
  if (Sequencer.timeToNextStepUs() < OLED_REDRAW_GATE_US) return;

  draw();
  dirty      = false;
  lastDrawMs = now;
}

void DisplayClass::draw() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  // --- header: mode | [C]hain flag + pattern + play arrow + BPM ---
  oled.setCursor(0, 0);
  oled.print(MODE_TITLES[appMode]);
  char hdr[14];
  snprintf(hdr, sizeof(hdr), "%sP%u %s%u",
           chain.active ? "C" : "",
           globalState.currentPattern + 1,
           Sequencer.isRunning() ? ">" : "",
           globalState.bpm);
  oled.setCursor(128 - (int)strlen(hdr) * 6, 0);
  oled.print(hdr);
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  // --- four encoder columns from the shared binding table ---
  const Param* b = ControlsClass::bindings(appMode);
  for (uint8_t i = 0; i < 4; i++) {
    int x = i * 32;
    if (i) oled.drawFastVLine(x - 1, 14, 38, SSD1306_WHITE);

    oled.setCursor(x + 1, 16);
    oled.print(Controls.paramLabel(b[i]));

    char val[8];
    Controls.paramValue(b[i], val, sizeof(val));
    oled.setCursor(x + 1, 30);
    oled.print(val);

    int bar = Controls.paramBar(b[i]);
    if (bar >= 0) {
      oled.drawRect(x + 1, 44, 28, 6, SSD1306_WHITE);
      if (bar > 0) oled.fillRect(x + 1, 44, 28 * bar / 100, 6, SSD1306_WHITE);
    }
  }

  if (appMode == MODE_SAMPLE_RECORD) {
    // --- status line: record state + live input level meter ---
    static const char* const SMP_TAGS[3] = { "rdy", "ARM", "REC" };
    oled.setCursor(0, 56);
    oled.print(SMP_TAGS[Sampler.state()]);
    int lvl = Sampler.level();          // 0..100
    if (lvl > 100) lvl = 100;
    oled.drawRect(24, 56, 102, 7, SSD1306_WHITE);
    if (lvl > 0) oled.fillRect(25, 57, lvl, 5, SSD1306_WHITE);
  } else {
    // --- status line: selected voice (+ M/S flags) + octave ---
    const Voice& vc = pattern.voices[editState.voice];
    char flags[4] = "";
    if (vc.mute) strlcat(flags, "M", sizeof(flags));
    if (vc.solo) strlcat(flags, "S", sizeof(flags));
    char st[26];
    snprintf(st, sizeof(st), "V%u%s %s oct%+d", editState.voice + 1,
             flags, SampleStore.name(vc.sampleId), editState.octaveShift);
    oled.setCursor(0, 56);
    oled.print(st);
  }

  // --- flash overlay: inverted box, centered ---
  if (flashUntil) {
    int w = (int)strlen(flashMsg) * 6;
    oled.fillRect(8, 22, 112, 20, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor((128 - w) / 2, 28);
    oled.print(flashMsg);
    oled.setTextColor(SSD1306_WHITE);
  }

  oled.display();    // ~23 ms blocking I2C — caller already gated this
}
