#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ===== Pins =====
#define DOT_BTN_PIN 13  // Tactile switch to GND (dot)
#define DASH_BTN_PIN 14 // Tactile switch to GND (dash)
#define OK_BTN_PIN 27   // Tactile switch to GND (OK: short=commit, long=clear)
#define BUZZER_PIN 18   // Active buzzer I/O

// Your buzzer is active-LOW (buzzes when pin is LOW)
#define BUZZER_ACTIVE_LOW 1

// ===== OLED (SH1106) =====
#define OLED_W 128
#define OLED_H 64
#define OLED_RESET -1
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
const uint8_t OLED_ADDR_PRIMARY = 0x3C;
const uint8_t OLED_ADDR_FALLBACK = 0x3D;
Adafruit_SH1106G display(OLED_W, OLED_H, &Wire, OLED_RESET);

// ===== Morse timing =====
uint16_t UNIT_MS = 120;                       // 1 unit = dot
uint16_t DOT_MAX_MS = (uint16_t)(1.5f * 120); // not used in 2-key input, kept for reference
uint16_t LETTER_GAP_MS = 3 * 120;             // still used for idle auto-commit (optional)
uint16_t WORD_GAP_MS = 7 * 120;               // idle auto-space
const uint16_t DEBOUNCE_MS = 25;
const uint16_t CLEAR_HOLD_MS = 2000; // OK long-press to clear

// ===== Buffer / display limits =====
const size_t MAX_TEXT_LEN = 120;   // hard cap for decodedText (prevents unbounded growth)
const size_t OLED_TAIL_CHARS = 40; // how many chars we draw on the OLED last line

// ===== Button state =====
struct Btn
{
  uint8_t pin;
  bool stable; // debounced pressed state (true when pressed)
  bool prevStable;
  uint32_t lastEdgeMs; // for debouncing
  uint32_t pressStartMs;
};

Btn btnDot{DOT_BTN_PIN, false, false, 0, 0};
Btn btnDash{DASH_BTN_PIN, false, false, 0, 0};
Btn btnOk{OK_BTN_PIN, false, false, 0, 0};

bool prevAnyPressed = false;
uint32_t lastSilenceStartMs = 0; // when all input buttons became unpressed
bool okClearLatched = false;     // to avoid repeating CLEAR during the same long hold

// ===== Buzzer helpers =====
inline void buzzerOn() { digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LOW ? LOW : HIGH); }
inline void buzzerOff() { digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LOW ? HIGH : LOW); }

// ===== Morse table =====
typedef struct
{
  const char *pattern;
  char ch;
} MorseEntry;
const MorseEntry MORSE_TABLE[] = {
    {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'}, {".", 'E'}, {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'}, {"..", 'I'}, {".---", 'J'}, {"-.-", 'K'}, {".-..", 'L'}, {"--", 'M'}, {"-.", 'N'}, {"---", 'O'}, {".--.", 'P'}, {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'}, {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'}, {"-.--", 'Y'}, {"--..", 'Z'}, {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'}, {"....-", '4'}, {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'}, {"----.", '9'}, {".-.-.-", '.'}, {"--..--", ','}, {"..--..", '?'}, {".----.", '\''}, {"-.-.--", '!'}, {"-..-.", '/'}, {"-.--.", '('}, {"-.--.-", ')'}, {".-...", '&'}, {"---...", ':'}, {"-.-.-.", ';'}, {"-...-", '='}, {".-.-.", '+'}, {"-....-", '-'}, {"..--.-", '_'}, {".-..-.", '"'}, {".--.-.", '@'}};
const size_t MORSE_TABLE_LEN = sizeof(MORSE_TABLE) / sizeof(MORSE_TABLE[0]);

char decodeMorse(const String &pattern)
{
  for (size_t i = 0; i < MORSE_TABLE_LEN; i++)
    if (pattern.equals(MORSE_TABLE[i].pattern))
      return MORSE_TABLE[i].ch;
  return '?';
}

// ===== Buffers =====
String currentSymbols = "";  // sequence of '.' and '-' for current letter
String decodedText = "";     // accumulated decoded output (tail shown on OLED)
bool textWasTrimmed = false; // set true when we trimmed old chars

// ===== Utilities =====
inline bool rawPressed(uint8_t pin) { return digitalRead(pin) == LOW; } // active-LOW buttons
inline bool anyPressed() { return btnDot.stable || btnDash.stable; }    // buzzer follows these

void applyUnit(uint16_t unit)
{
  UNIT_MS = unit;
  DOT_MAX_MS = (uint16_t)(1.5f * unit);
  LETTER_GAP_MS = 3 * unit;
  WORD_GAP_MS = 7 * unit;
}

// Trim helper: keep only the last MAX_TEXT_LEN chars
void ensureTextLimit()
{
  if (decodedText.length() > MAX_TEXT_LEN)
  {
    decodedText.remove(0, decodedText.length() - MAX_TEXT_LEN);
    textWasTrimmed = true;
  }
}

void pushChar(char c)
{
  decodedText += c;
  ensureTextLimit();
}

void pushSpaceIfNeeded()
{
  if (decodedText.length() == 0)
    return;
  if (decodedText[decodedText.length() - 1] != ' ')
  {
    decodedText += ' ';
    ensureTextLimit();
  }
}

void commitLetterIfAny()
{
  if (currentSymbols.length() == 0)
    return;
  char c = decodeMorse(currentSymbols);
  pushChar(c);
  Serial.printf("LETTER: %s -> %c\n", currentSymbols.c_str(), c);
  currentSymbols = "";
}

void clearAll()
{
  decodedText = "";
  currentSymbols = "";
  textWasTrimmed = false;
  Serial.println("** CLEAR **");
}

// Debounce + edge detect for a single button.
// Returns: 0=no event, +1=pressed, -1=released
int8_t updateButton(Btn &b, uint32_t now)
{
  bool raw = rawPressed(b.pin);
  if (raw != b.stable)
  {
    if (now - b.lastEdgeMs >= DEBOUNCE_MS)
    {
      b.prevStable = b.stable;
      b.stable = raw;
      b.lastEdgeMs = now;
      if (b.stable)
      {
        b.pressStartMs = now;
        return +1;
      } // pressed
      else
      {
        return -1;
      } // released
    }
  }
  else
  {
    b.lastEdgeMs = now;
  }
  return 0;
}

// ===== OLED UI =====
void drawUI()
{
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("ESP32 Morse (3-btn)");

  display.setCursor(0, 10);
  display.print("u=");
  display.print(UNIT_MS);
  display.print("ms  jrcsrg");

  display.setCursor(0, 22);
  display.print("DOT:");
  display.print(btnDot.stable ? "DOWN" : "UP  ");
  display.setCursor(64, 22);
  display.print("DASH:");
  display.print(btnDash.stable ? "DOWN" : "UP  ");

  display.setCursor(0, 34);
  display.print("OK :");
  display.print(btnOk.stable ? "DOWN" : "UP  ");
  display.setCursor(64, 34);

  display.setCursor(0, 46);
  display.print("Letter: ");
  display.print(currentSymbols);

  display.setCursor(0, 56);
  // Show tail; add "…" if trimmed
  String tail = decodedText;
  bool showEllipsis = textWasTrimmed && tail.length() > OLED_TAIL_CHARS;
  if (tail.length() > OLED_TAIL_CHARS)
  {
    tail = tail.substring(tail.length() - OLED_TAIL_CHARS);
  }
  if (showEllipsis)
    display.print("...");
  display.print(tail);

  display.display();
}

// ===== Setup / Loop =====
void setup()
{
  Serial.begin(115200);
  delay(150);

  pinMode(DOT_BTN_PIN, INPUT_PULLUP);
  pinMode(DASH_BTN_PIN, INPUT_PULLUP);
  pinMode(OK_BTN_PIN, INPUT_PULLUP);

  // Ensure silent at boot for active-LOW:
  if (BUZZER_ACTIVE_LOW)
    digitalWrite(BUZZER_PIN, HIGH);
  else
    digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  buzzerOff();

  btnDot.stable = rawPressed(DOT_BTN_PIN);
  btnDash.stable = rawPressed(DASH_BTN_PIN);
  btnOk.stable = rawPressed(OK_BTN_PIN);
  btnDot.prevStable = btnDot.stable;
  btnDash.prevStable = btnDash.stable;
  btnOk.prevStable = btnOk.stable;
  btnDot.lastEdgeMs = millis();
  btnDash.lastEdgeMs = millis();
  btnOk.lastEdgeMs = millis();
  lastSilenceStartMs = millis();
  prevAnyPressed = (btnDot.stable || btnDash.stable);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!display.begin(OLED_ADDR_PRIMARY, true))
  {
    display.begin(OLED_ADDR_FALLBACK, true);
  }
  display.clearDisplay();
  display.setRotation(0);
  display.display();

  // Splash
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.print("Morse Ready (3-btn)");
  display.setCursor(0, 12);
  display.print("DOT=13 DASH=14 OK=27");
  display.setCursor(0, 24);
  display.print("Press=Buzz (active-LOW)");
  display.setCursor(0, 36);
  display.print("OK: short=commit");
  display.setCursor(0, 48);
  display.print("OK: long(2s)=CLEAR");
  display.display();
  delay(5000);
}

void loop()
{
  uint32_t now = millis();

  // Update buttons
  int8_t evDot = updateButton(btnDot, now);
  int8_t evDash = updateButton(btnDash, now);
  int8_t evOk = updateButton(btnOk, now);

  // Buzzer only follows DOT/DASH (not OK)
  bool nowAnyPressed = (btnDot.stable || btnDash.stable);
  if (nowAnyPressed)
    buzzerOn();
  else
    buzzerOff();

  // OK long-press = CLEAR (latch once per hold)
  if (btnOk.stable && !okClearLatched && (now - btnOk.pressStartMs >= CLEAR_HOLD_MS))
  {
    clearAll();
    okClearLatched = true; // prevent repeat clear until OK is released
  }
  if (!btnOk.stable)
    okClearLatched = false;

  // On release of DOT/DASH, append the corresponding symbol
  if (evDot == -1)
  {
    currentSymbols += '.';
    Serial.println("DOT");
  }
  if (evDash == -1)
  {
    currentSymbols += '-';
    Serial.println("DASH");
  }

  // On OK short press (release before CLEAR_HOLD_MS), commit current letter
  if (evOk == -1)
  {
    uint32_t held = now - btnOk.pressStartMs;
    if (held < CLEAR_HOLD_MS)
    {
      commitLetterIfAny(); // finalize letter
      Serial.println("OK: COMMIT");
    }
  }

  // Optional: auto-commit letter/space on idle gaps (still handy if you forget OK)
  if (!prevAnyPressed && nowAnyPressed)
  {
    uint32_t gap = now - lastSilenceStartMs;
    if (gap >= WORD_GAP_MS)
    {
      commitLetterIfAny();
      pushSpaceIfNeeded();
      Serial.println("GAP: WORD (auto)");
    }
    else if (gap >= LETTER_GAP_MS)
    {
      commitLetterIfAny();
      Serial.println("GAP: LETTER (auto)");
    }
  }

  // Track when full silence starts (no DOT/DASH pressed)
  if (prevAnyPressed && !nowAnyPressed)
  {
    lastSilenceStartMs = now;
  }
  prevAnyPressed = nowAnyPressed;

  drawUI();
  delay(5);
}
