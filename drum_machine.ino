#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- Audio graph ---
AudioSynthWaveform    osc;
AudioSynthNoiseWhite  noise;
AudioSynthWaveform    clickOsc;
AudioMixer4           mixer;
AudioOutputI2S        i2sOut;
AudioControlSGTL5000  sgtl5000;

AudioConnection patchKick (osc,      0, mixer, 0);
AudioConnection patchHat  (noise,    0, mixer, 1);
AudioConnection patchClick(clickOsc, 0, mixer, 2);
AudioConnection patchMixL (mixer,    0, i2sOut, 0);
AudioConnection patchMixR (mixer,    0, i2sOut, 1);

// --- RGBW NeoPixels ---
// Change NEO_GRBW to NEO_RGBW if colours appear in the wrong order
#define NUM_LEDS     16
#define LED_DATA_PIN  8
Adafruit_NeoPixel strip(NUM_LEDS, LED_DATA_PIN, NEO_GRBW + NEO_KHZ800);

// --- SSD1306 (I2C pins 18/19, shared with audio shield) ---
// Many cheap SSD1306 modules have a hardware two-colour panel:
// top ~16 rows are yellow, the rest blue — the code is laid out to use this.
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const int BUTTON_PIN = 31;
const int STEPS      = 16;
const int VOICES     = 2;

const int MODE_KICK  = 0;
const int MODE_HIHAT = 1;
const int MODE_RESET = 2;
const int MODE_CLICK = 3;
const int MODE_BPM   = 4;
const int MODE_COUNT = 5;

const unsigned long KICK_MS       = 300;
const unsigned long HAT_MS        = 80;
const unsigned long CLICK_MS      = 20;
const unsigned long DEBOUNCE_MS   = 80;
const unsigned long LONG_PRESS_MS = 500;

int currentBPM = 120;

inline unsigned long stepMs() { return 60000UL / (unsigned long)currentBPM / 4; }

bool pattern[VOICES][STEPS];
int  currentStep  = STEPS - 1;
int  pressedStep  = 0;   // step captured at press time, used on release
int  activeMode   = MODE_KICK;
bool clickEnabled = true;

unsigned long nextStepTime        = 0;
unsigned long triggerTime[VOICES] = {0, 0};
unsigned long clickTriggerTime    = 0;

// Button state machine
bool          rawPrev          = HIGH;
bool          debouncedState   = HIGH;
unsigned long debounceTimer    = 0;
unsigned long pressStartTime   = 0;
bool          longPressHandled = false;
bool          buttonHeld       = false;

bool displayNeedsUpdate = true;

// ---------------------------------------------------------------------------
// Icons — 20×20 px, colour passed in so they work on both white and black bg

void drawIconKick(int ox, int oy, uint16_t c) {
  uint16_t ci = c ? SSD1306_BLACK : SSD1306_WHITE;
  display.fillCircle   (ox+10, oy+13, 7,    c);
  display.fillCircle   (ox+10, oy+13, 4,   ci);
  display.fillRect     (ox+9,  oy+3,  2, 7,  c);
  display.fillCircle   (ox+10, oy+3,  3,    c);
}

void drawIconHihat(int ox, int oy, uint16_t c) {
  display.fillRoundRect(ox,    oy+1,  20, 5, 2, c);
  display.drawLine     (ox+10, oy+6,  ox+10, oy+14, c);
  display.fillRoundRect(ox+3,  oy+13, 14, 5, 2, c);
  display.fillRect     (ox+6,  oy+18,  8, 2,    c);
}

void drawIconReset(int ox, int oy, uint16_t c) {
  display.drawRoundRect(ox+7,  oy,    6, 3,  1, c);
  display.fillRect     (ox+3,  oy+3, 14, 2,     c);
  display.drawRoundRect(ox+4,  oy+6, 12, 13, 1, c);
  display.drawLine     (ox+8,  oy+8, ox+8,  oy+17, c);
  display.drawLine     (ox+10, oy+8, ox+10, oy+17, c);
  display.drawLine     (ox+12, oy+8, ox+12, oy+17, c);
}

void drawIconClick(int ox, int oy, uint16_t c) {
  display.fillCircle(ox+6,  oy+15,  4,    c);
  display.fillRect  (ox+9,  oy+3,   2, 13, c);
  display.drawLine  (ox+10, oy+3,  ox+18, oy+7,  c);
  display.drawLine  (ox+11, oy+6,  ox+18, oy+10, c);
}

void drawIconBPM(int ox, int oy, uint16_t c) {
  uint16_t ci = c ? SSD1306_BLACK : SSD1306_WHITE;
  display.fillTriangle(ox+2,  oy+19, ox+18, oy+19, ox+10, oy+1, c);
  display.drawLine    (ox+10, oy+7,  ox+15, oy+16, ci);
  display.fillCircle  (ox+10, oy+7,  2, ci);
}

// ---------------------------------------------------------------------------

void drawPatternRow(int voice, int y) {
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, y);
  display.print(voice == 0 ? "K" : "H");
  for (int i = 0; i < STEPS; i++) {
    int x = 10 + i * 7;
    if (pattern[voice][i]) {
      display.fillRect (x, y, 5, 7, SSD1306_WHITE);
    } else {
      display.drawPixel(x + 2, y + 3, SSD1306_WHITE);
    }
  }
}

void drawDisplay() {
  display.clearDisplay();

  // ── Yellow zone (y 0-15): white-filled header, black content ─────────────
  display.fillRect(0, 0, 128, 16, SSD1306_WHITE);

  switch (activeMode) {
    case MODE_KICK:  drawIconKick (0, 0, SSD1306_BLACK); break;
    case MODE_HIHAT: drawIconHihat(0, 0, SSD1306_BLACK); break;
    case MODE_RESET: drawIconReset(0, 0, SSD1306_BLACK); break;
    case MODE_CLICK: drawIconClick(0, 0, SSD1306_BLACK); break;
    case MODE_BPM:   drawIconBPM  (0, 0, SSD1306_BLACK); break;
  }

  const char* modeNames[] = { "KICK", "HIHAT", "RESET", "CLICK", "BPM" };
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(23, 4);
  display.print(modeNames[activeMode]);

  // HELD badge — black box with white text so it stands out on white header
  if (buttonHeld) {
    display.fillRect(94, 2, 32, 12, SSD1306_BLACK);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(96, 4);
    display.print("HELD");
  }

  // ── Blue zone (y 16-63): black background, white content ─────────────────
  display.drawFastHLine(0, 17, 128, SSD1306_WHITE);

  display.setTextSize(1);
  drawPatternRow(0, 20);
  drawPatternRow(1, 30);

  display.drawFastHLine(0, 41, 128, SSD1306_WHITE);

  // Mode hint — inverted (white bg, black text) so it pops against the blue zone
  display.fillRect(0, 43, 128, 21, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 46);
  switch (activeMode) {
    case MODE_KICK:
    case MODE_HIHAT:
      display.print("HOLD: CHANGE MODE");
      break;
    case MODE_RESET:
      display.print("TAP: CLEAR ALL");
      break;
    case MODE_CLICK:
      display.print(clickEnabled ? "CLICK: ON " : "CLICK: OFF");
      display.setCursor(2, 55);
      display.print("TAP: TOGGLE");
      break;
    case MODE_BPM:
      display.setTextSize(2);
      display.setCursor(2, 46);
      display.print(currentBPM);
      display.print(" BPM");
      display.setTextSize(1);
      break;
  }

  display.display();
  displayNeedsUpdate = false;
}

// ---------------------------------------------------------------------------

void drawLEDs() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint32_t color;
    if (i == currentStep) {
      color = strip.Color(0, 0, 0, 200);         // white via W channel
    } else {
      bool kick = pattern[0][i];
      bool hat  = pattern[1][i];
      if      (kick && hat) color = strip.Color(180, 130,   0, 0); // yellow
      else if (kick)        color = strip.Color(200,   0,   0, 0); // red
      else if (hat)         color = strip.Color(  0, 200,   0, 0); // green
      else                  color = strip.Color(  0,   0,   0, 5); // dim W
    }
    strip.setPixelColor(i, color);
  }
  strip.show();
}

// ---------------------------------------------------------------------------

void trigger(int voice) {
  triggerTime[voice] = millis();
}

void setup() {
  AudioMemory(20);

  sgtl5000.enable();
  Wire.setClock(400000);
  sgtl5000.volume(0.65f);
  sgtl5000.lineOutLevel(13);
  delay(500);

  osc.begin(WAVEFORM_SINE);
  osc.frequency(200.0f);
  osc.amplitude(0.0f);

  noise.amplitude(0.0f);

  clickOsc.begin(WAVEFORM_SINE);
  clickOsc.frequency(1000.0f);
  clickOsc.amplitude(0.0f);

  mixer.gain(0, 1.0f);
  mixer.gain(1, 0.4f);
  mixer.gain(2, 0.25f);
  mixer.gain(3, 0.0f);

  memset(pattern, 0, sizeof(pattern));

  strip.begin();
  strip.setBrightness(80);
  strip.clear();
  strip.show();

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  drawDisplay();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  nextStepTime = millis();
}

void loop() {
  unsigned long now = millis();

  // --- Sequencer (first — keeps timing tight) ---
  if (now >= nextStepTime) {
    currentStep = (currentStep + 1) % STEPS;

    for (int v = 0; v < VOICES; v++) {
      if (pattern[v][currentStep]) trigger(v);
    }

    if (currentStep % 4 == 0 && clickEnabled) clickTriggerTime = now;

    drawLEDs();
    nextStepTime = now + stepMs();
  }

  // --- Voice 0: kick ---
  unsigned long e0 = now - triggerTime[0];
  if (e0 < KICK_MS) {
    float t = (float)e0 / 1000.0f;
    osc.frequency(50.0f + 150.0f * expf(-t / 0.030f));
    osc.amplitude(expf(-t / 0.100f));
  } else {
    osc.amplitude(0.0f);
    osc.frequency(200.0f);
  }

  // --- Voice 1: hi-hat ---
  unsigned long e1 = now - triggerTime[1];
  if (e1 < HAT_MS) {
    noise.amplitude(expf(-(float)e1 / 1000.0f / 0.030f));
  } else {
    noise.amplitude(0.0f);
  }

  // --- Click track ---
  unsigned long ec = now - clickTriggerTime;
  if (ec < CLICK_MS) {
    clickOsc.amplitude(1.0f - (float)ec / CLICK_MS);
  } else {
    clickOsc.amplitude(0.0f);
  }

  // --- Button: stable-signal debounce ---
  bool raw = digitalRead(BUTTON_PIN);
  if (raw != rawPrev) {
    debounceTimer = now;
    rawPrev = raw;
  }
  if ((now - debounceTimer) >= DEBOUNCE_MS && raw != debouncedState) {
    debouncedState = raw;
    if (debouncedState == LOW) {
      pressedStep      = currentStep;   // capture step at press, not release
      pressStartTime   = now;
      longPressHandled = false;
      buttonHeld       = true;
      displayNeedsUpdate = true;
    } else {
      buttonHeld = false;
      if (!longPressHandled) {
        switch (activeMode) {
          case MODE_KICK:
          case MODE_HIHAT:
            pattern[activeMode][pressedStep] = !pattern[activeMode][pressedStep];
            drawLEDs();
            break;
          case MODE_RESET:
            memset(pattern, 0, sizeof(pattern));
            drawLEDs();
            break;
          case MODE_CLICK:
            clickEnabled = !clickEnabled;
            break;
          case MODE_BPM:
            currentBPM += 10;
            if (currentBPM > 140) currentBPM = 60;
            break;
        }
      }
      displayNeedsUpdate = true;
    }
  }

  // Long press: cycle mode
  if (debouncedState == LOW && !longPressHandled &&
      (now - pressStartTime) >= LONG_PRESS_MS) {
    longPressHandled = true;
    activeMode = (activeMode + 1) % MODE_COUNT;
    displayNeedsUpdate = true;
  }

  // Display: update only when needed and not within 30ms of next tick
  if (displayNeedsUpdate && (nextStepTime - now) > 30UL) {
    drawDisplay();
  }
}
