
# ESP32 Morse Buzzer Tester (3-Button, SH1106 OLED)

A simple Morse “keyer” for ESP32 that uses **two buttons** for `DOT` and `DASH`, plus a third **OK** button:

* **DOT** button → adds `.` on release (buzzer sounds while held)
* **DASH** button → adds `-` on release (buzzer sounds while held)
* **OK** (GPIO27):

  * **Short press** → **commit** current `.-` as a **letter**
  * **Triple short press** → **playback**:

    * If you’re building a letter, plays that pattern
    * Otherwise plays the **entire committed message** (e.g., “SOS”) with proper Morse timing
  * **Long press (≥2s)** → **clear** text and current pattern

Live status appears on a **128×64 SH1106 I²C OLED**, and events mirror to **Serial**.

> Designed in PlatformIO - Arduino framework.
> Uses an existing deps: **Adafruit GFX** and **Adafruit SH110X**.

---

## Features

* 3-button input with debounced press/release
* **Minimal OLED UI** (no hints)

  * Header indicates **PLAYING**
  * Line 2 shows `u=<unit>ms` and **`jrcsrg`** when playing
  * Current `Letter:` (when idle) or “PLAYING MSG…” (during playback)
  * Text tail (auto-trim with leading “…”)
* **Active-LOW** buzzer (silent by default)
* Correct Morse timing:

  * `.` = **1 unit** tone, `-` = **3 units** tone
  * **1u** between parts of a letter, **3u** between letters, **7u** between words
  * Repeats message with a **3u** loop gap (configurable)
* Auto-trim text buffer to prevent RAM growth

---

## Bill of Materials

* ESP32 DevKit board (`board = esp32dev`)
* SH1106 128×64 OLED (I²C)
* 3 × tactile push buttons
* 1 × **active buzzer module** (3-pin: VCC / I/O / GND) — **active-LOW** behavior supported
* Jumper wires

---

## Pinout (defaults in code)

| Function    | ESP32 GPIO |
| ----------- | ---------- |
| OLED SDA    | **21**     |
| OLED SCL    | **22**     |
| DOT button  | **13**     |
| DASH button | **14**     |
| OK button   | **27**     |
| Buzzer I/O  | **18**     |
| 3.3V power  | 3.3V       |
| Ground      | GND        |

> Buttons are wired **active-LOW** (to `INPUT_PULLUP`).
> Buzzer defaults to **active-LOW** (set in code) so it’s **silent when idle**.

---

## Wiring Schematics

### 1) I²C OLED (SH1106)

```
ESP32 (3.3V)                SH1106 OLED
-----------                 -----------
3V3  ---------------------> VCC
GND  ---------------------> GND
GPIO22 -------------------> SCL
GPIO21 -------------------> SDA
```

### 2) Buttons (active-LOW to GND)

```
DOT button                                DASH button                               OK button
----------                                 ----------                               ---------
   ┌─────┐                                    ┌─────┐                                 ┌─────┐
   │     │                                    │     │                                 │     │
GND│  o  ├─┐                              GND │  o  ├─┐                           GND │  o  ├─┐
   │     │ │                                  │     │ │                               │     │ │
   └─o───┘ │                                  └─o───┘ │                               └─o───┘ │
           └──────────────> ESP32 GPIO13                  └────────────> ESP32 GPIO14           └────────────> ESP32 GPIO27
```

> No external resistors needed — each pin is configured with `INPUT_PULLUP`.
> When a button is pressed, the pin reads **LOW** (pressed).

### 3) Active Buzzer (3-pin)

```
ESP32 (3.3V)           Active Buzzer Module
-----------            --------------------
3V3  ----------------> VCC
GND  ----------------> GND
GPIO18 --------------> I/O    (logic input)

(Active-LOW assumed: I/O = LOW → buzz; HIGH → silent)
```

> If your buzzer is **active-HIGH**, flip the code flag (see “Configuration”).
> If you discover your buzzer is **5V-only**, power it at 5V and use a small NPN/MOSFET level driver for I/O (ask if you want a tiny sketch).

---

## Project Setup (PlatformIO)

**`platformio.ini`** (compatible with your existing config)

```ini
[env:esp32dev]
platform = espressif32@6.12.0
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600

lib_deps =
  adafruit/Adafruit GFX Library @ ^1.12.1
  adafruit/Adafruit SH110X @ ^2.1.14
```

> You can keep other libs you already have; only GFX + SH110X are needed here.

**Project structure**

```
.
├── platformio.ini
└── src
    └── main.cpp   <-- use the 3-button Morse code file I provided
```

---

## Controls & Behavior

* **DOT button** (GPIO13): hold = buzzer ON; release = append `.`
* **DASH button** (GPIO14): hold = buzzer ON; release = append `-`
* **OK button** (GPIO27):

  * **Short press** → commit current `.-` into a **letter**
  * **Long press (≥ 2s)** → **clear** all text and current letter buffer
* **Auto commit on silence** (optional):

  * **3 × unit** of silence → commit letter
  * **7 × unit** of silence → insert space
* **Unit time** (`UNIT_MS`): default **120 ms** (adjustable)

---

## Configuration (in `main.cpp`)

At the top of the file, you can change:

```cpp
// Buttons
#define DOT_BTN_PIN    13
#define DASH_BTN_PIN   14
#define OK_BTN_PIN     27

// Buzzer (3-pin active module)
#define BUZZER_PIN     18

// Polarity: 1 = active-LOW (LOW=ON), 0 = active-HIGH (HIGH=ON)
#define BUZZER_ACTIVE_LOW 1

// Morse timing (1 "unit")
uint16_t UNIT_MS = 120;  // try 100–150 for comfortable pacing

// Text buffer caps (prevents unbounded growth)
const size_t MAX_TEXT_LEN      = 120; // keep only last N chars in memory
const size_t OLED_TAIL_CHARS   = 40;  // show last N chars on the OLED line
```

> The code **preloads** the buzzer pin **HIGH** before `pinMode(OUTPUT)` when `BUZZER_ACTIVE_LOW = 1` to avoid a boot chirp.

---

## Usage

1. Wire OLED, buttons, and buzzer per schematics above.
2. Build & upload with PlatformIO; open Serial Monitor at **115200**.
3. **Tap/hold DOT/DASH** to build a `.-` sequence; **release** to append (`.` or `-`).
4. **OK short** commits the current sequence to a **letter**.
5. **OK triple short**:

   * If a letter is in progress → plays that **pattern**.
   * Otherwise → plays the **entire committed text** (e.g., “SOS”), looping.
6. **OK long (≥2s)** clears everything.
7. Any button press **stops playback**.

---

## Configuration (in `main.cpp`)

```cpp
// Pins
#define DOT_BTN_PIN    13
#define DASH_BTN_PIN   14
#define OK_BTN_PIN     27
#define BUZZER_PIN     18

// Buzzer polarity: 1 = active-LOW (LOW=ON), 0 = active-HIGH (HIGH=ON)
#define BUZZER_ACTIVE_LOW 1

// Morse timing
uint16_t UNIT_MS = 120;          // 1 unit (dot)
uint16_t LETTER_GAP_MS = 3 * 120;
uint16_t WORD_GAP_MS   = 7 * 120;

// Playback loop gap (between full message repeats)
const uint16_t PLAY_LOOP_GAP_MS = 3 * 120;

// Text buffer limits
const size_t MAX_TEXT_LEN    = 120;
const size_t OLED_TAIL_CHARS = 40;
```

> Want a slower/faster keyer? Change `UNIT_MS` (e.g., 100–150).
> Want a longer pause between message repeats? Increase `PLAY_LOOP_GAP_MS` (e.g., `7 * 120`).

---

## OLED Layout (Minimal)

* **Line 1**: `ESP32 Morse (3-btn)` or `ESP32 Morse (PLAYING)`
* **Line 2**: `u=<ms>` and **`jrcsrg`** when playing
* **Line 3**: `DOT/DASH` key states
* **Line 4**: `Letter: .-` (idle) or `PLAYING MSG...` (playing)
* **Line 5**: `Text:` tail (with leading `…` if trimmed)

---

## Troubleshooting

* **Play toggle ON but no sound**

  * The code won’t start playback if there’s no sequence. Make sure:

    * You have an in-progress letter **or** committed text (e.g., commit `S`, `O`, `S`).
  * Confirm **buzzer polarity**: set `BUZZER_ACTIVE_LOW` to `1` (LOW=ON).
  * Ensure **BUZZER\_PIN = 18** is connected to the module **I/O**, not VCC.

* **Buzzer constantly on**

  * Invert `BUZZER_ACTIVE_LOW` (0 ↔ 1).
  * Double-check wiring: the I/O pin should not be tied to 3V3.

* **Boot chirp**

  * The code preloads the pin to keep it HIGH before `pinMode(OUTPUT)`.
  * If it still chirps, add **10k pull-up** from buzzer I/O to **3.3V**.

* **OLED blank**

  * Check `SDA=21`, `SCL=22`. The sketch tries **0x3C**, then **0x3D**.


---

## License

MIT License

Copyright (c) 2025 Mat Jerico Sergio

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

## Changelog (recent)

* **Triple-tap OK** now plays the **entire committed message** (letter-by-letter with proper gaps) if no letter is currently being built.
* Playback engine fixed to avoid skipping first symbol after loop gaps.
* Minimal OLED UI; shows **`jrcsrg`** only during playback.
* Text buffer trimmed with visible leading `…` when older text is removed.


---
## Credits

* Adafruit **GFX** + **SH110X** for OLED support
* PlatformIO + Arduino for smooth dev flow


