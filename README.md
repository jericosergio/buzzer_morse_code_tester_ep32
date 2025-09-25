
# ESP32 Morse Buzzer Tester (3-Button, SH1106 OLED)

A simple Morse “keyer” for ESP32 that uses **two buttons** for `DOT` and `DASH`, plus a third **OK** button:

* **DOT** button → adds `.` on release (buzzer sounds while held)
* **DASH** button → adds `-` on release (buzzer sounds while held)
* **OK** button → **short press** commits the current `.-` sequence into a **letter**, **long press (≥2s)** clears the **text buffer**

Live status appears on a **128×64 SH1106 I²C OLED**, and events mirror to **Serial**.

> Designed in PlatformIO - Arduino framework.
> Uses an existing deps: **Adafruit GFX** and **Adafruit SH110X**.

---

## Features

* 3-button input: DOT, DASH, OK (commit/clear)
* Buzzer **only** when DOT/DASH are pressed (silent otherwise)
* Debounce + clean press/release handling
* Optional **auto letter/word commit** based on silence gaps (3u/7u)
* Buffer capped to avoid unlimited growth (shows ellipsis when trimmed)
* OLED status: key states, current `.-` sequence, and recent decoded text

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

1. Wire OLED, buttons, and buzzer as shown.
2. Build & upload via PlatformIO.
3. Open Serial Monitor at **115200**.
4. Tap/hold DOT & DASH to build a `.-` sequence; press **OK** to commit to a letter.
5. Long-press **OK** (≥2s) to clear the text.
6. Watch the OLED for **key states**, **current sequence**, and the **decoded text tail** (shows `…` when older text is trimmed).

---

## Troubleshooting

* **Buzzer is ON constantly / inverted behavior**
  → Set `#define BUZZER_ACTIVE_LOW 1` (active-LOW) or `0` (active-HIGH).
  → Ensure the buzzer **I/O** is on **GPIO18**, not tied to 3V3.

* **Boot chirp** (buzz right at reset)
  → The code preloads the latch to keep it silent. If it still chirps, add a **10k pull-up** from **BUZZER I/O** to **3.3V**.

* **OLED blank**
  → Double-check SDA=21, SCL=22; try address **0x3C** vs **0x3D**.

* **Buttons not responding**
  → Confirm each button’s other leg goes to **GND** and `pinMode(..., INPUT_PULLUP)` is present.

* **Text grows too long**
  → The buffer auto-trims to `MAX_TEXT_LEN`. Increase/decrease as desired.

---

## Optional: 5V-only Buzzer Driver (quick sketch)

If your module needs **5V** and doesn’t accept 3.3V logic safely, use an NPN (2N2222):

```
ESP32 GPIO18 --1k--> NPN Base
NPN Emitter --------> GND
NPN Collector ------> Buzzer I/O (or low-side of the module input)
Buzzer VCC ---------> 5V
Buzzer GND ---------> GND
(Optionally, 10k from Base to GND as pulldown)
```

> Most active buzzers don’t require a flyback diode, but check your module.

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

## Credits

* Adafruit **GFX** + **SH110X** for OLED support
* PlatformIO + Arduino for smooth dev flow

---

