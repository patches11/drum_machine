# Teensy 3.6 Drum Machine — Pin Assignment Reference

A wiring one-pager. Assumes: **onboard SDIO** for storage (not the shield's SD slot, which frees pins 7/10/12/14), **Serial1 reserved for DIN MIDI**, OLED on the shared I²C bus, and one WS2812Serial data line for all 64 LEDs.

> Verify two things against current docs before soldering: the **PJRC Audio Shield pinout** for your revision, and the **WS2812Serial supported-pin list** for Teensy 3.6 (the LED pin must be on it).

---

## Reserved (do not reassign)

| Pin | Use |
|---|---|
| 9, 11, 13, 22, 23 | Audio shield I²S — BCLK, MCLK, RX(DOUT), TX(DIN), LRCLK |
| 18, 19 | I²C (SDA/SCL) — **shared** by codec **and** SSD1306 OLED |
| 0 | MIDI IN  — Serial1 RX (via opto-isolator) |
| 1 | MIDI OUT — Serial1 TX (via 2 resistors) |
| 8 | WS2812 LED data — Serial3 TX (used by WS2812Serial) |
| 7 | leave unused (Serial3 RX; keep the UART clear for the LED driver) |

OLED adds **zero** pins (different I²C address from the codec). LEDs take **one** pin.

---

## Encoders (direct, 2 pins each — all 3.6 pins are interrupt-capable)

| Encoder | A | B |
|---|---|---|
| Enc 1 | 24 | 25 |
| Enc 2 | 26 | 27 |
| Enc 3 | 28 | 29 |
| Enc 4 | 30 | 31 |

Encoder **push-switches** are not here — they go into the button matrix below.

---

## Button matrix — 5 × 5 (10 pins, holds 25 buttons; 21 used)

| Matrix line | Pin |
|---|---|
| Row 0 | 2 |
| Row 1 | 3 |
| Row 2 | 4 |
| Row 3 | 5 |
| Row 4 | 6 |
| Col 0 | 14 |
| Col 1 | 15 |
| Col 2 | 16 |
| Col 3 | 17 |
| Col 4 | 20 |

**Button layout in the grid:**

| | Col 0 | Col 1 | Col 2 | Col 3 | Col 4 |
|---|---|---|---|---|---|
| **Row 0** | C | C# | D | D# | E |
| **Row 1** | F | F# | G | G# | A |
| **Row 2** | A# | B | Accent | Transport | Mode |
| **Row 3** | Shift | Octave | EncPush 1 | EncPush 2 | EncPush 3 |
| **Row 4** | EncPush 4 | spare | spare | spare | spare |

Scan: set columns to `INPUT_PULLUP`, drive one row `LOW` at a time, read the columns (a pressed key reads `LOW`). **One diode per switch**, all oriented the same way (cathode toward the row line for the drive-rows / read-columns scheme above) → full N-key rollover, so chords and Shift+key never ghost. Match diode orientation to your scan code.

---

## Free / spare pins (11)

`10, 12, 21, 32, 33, 34, 35, 36, 37, 38, 39`

- For **FSR velocity sensing** later: pin **21 = A7** (analog) is the natural mux output for a 74HC4067 reading per-key force. The audio path uses I²S, so the ADC is entirely free.
- The rest are digital spares for extra buttons, a second encoder bank, or a status LED.

---

## Pin census (0–39)

| Pins | Count | Assigned to |
|---|---|---|
| 9, 11, 13, 18, 19, 22, 23 | 7 | Audio shield (+ OLED on 18/19) |
| 0, 1 | 2 | MIDI (Serial1) |
| 7, 8 | 2 | WS2812 LED driver (Serial3; 8 = data, 7 idle) |
| 24–31 | 8 | Encoders ×4 |
| 2–6, 14–17, 20 | 10 | Button matrix 5×5 |
| 10, 12, 21, 32–39 | 11 | Free / expansion |

Total: 40 pins → **29 used, 11 free.**

---

## Power & signal notes

- **WS2812 (64 LEDs):** power from a **dedicated 5 V supply**, not the Teensy 3.3 V regulator; common ground with the Teensy. Data on pin 8. 3.3 V data is usually accepted; if flaky, add a level shifter or keep the data run short, and put a ~330 Ω resistor in series at the first LED + a big cap across the strip's 5 V/GND.
- **Audio shield:** stacks on the Teensy, 3.3 V; sum L+R to mono for your single speaker.
- **OLED + codec on I²C:** both already pulled up by the shield; if the OLED module also has pull-ups, that's usually fine — only worry if the bus gets sluggish.
- **MIDI IN:** opto-isolator (6N138 / H11L1) output to pin 0; never connect MIDI IN ground directly to the Teensy. **MIDI OUT:** pin 1 through two 220 Ω resistors to the jack.
- **Encoders:** if you read jitter, add small RC debounce on the A/B lines or rely on the Encoder library + software filtering; push-switches debounce via Bounce2 through the matrix.
