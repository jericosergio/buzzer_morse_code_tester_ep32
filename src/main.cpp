#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ================= Pins =================
#define DOT_BTN_PIN 13  // DOT button to GND
#define DASH_BTN_PIN 14 // DASH button to GND
#define OK_BTN_PIN 27   // OK button to GND (short=commit, triple=loop, long=clear)
#define BUZZER_PIN 18   // Active buzzer I/O

// Active-LOW buzzer: LOW=ON, HIGH=OFF
#define BUZZER_ACTIVE_LOW 1

// ================= OLED (SH1106) =================
#define OLED_W 128
#define OLED_H 64
#define OLED_RESET -1
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
const uint8_t OLED_ADDR_PRIMARY = 0x3C;
const uint8_t OLED_ADDR_FALLBACK = 0x3D;
Adafruit_SH1106G display(OLED_W, OLED_H, &Wire, OLED_RESET);

// ================= Timing =================
uint16_t UNIT_MS = 120;           // dot duration
uint16_t LETTER_GAP_MS = 3 * 120; // silence between letters
uint16_t WORD_GAP_MS = 7 * 120;   // silence between words
const uint16_t DEBOUNCE_MS = 25;

const uint16_t CLEAR_HOLD_MS = 2000;     // OK long-press clears
const uint16_t OK_MULTI_WINDOW_MS = 600; // triple-tap window

// Playback timings (fixed to UNIT_MS at boot)
const uint16_t PLAY_DOT_MS = 1 * 120;       // tone
const uint16_t PLAY_DASH_MS = 3 * 120;      // tone
const uint16_t PLAY_INTER_GAP_MS = 1 * 120; // between parts of a letter
const uint16_t PLAY_LOOP_GAP_MS = 3 * 120;  // between full-message loops

// ================= Buffer / display caps =================
const size_t MAX_TEXT_LEN = 120;
const size_t OLED_TAIL_CHARS = 40;

// ================= Button state =================
struct Btn
{
  uint8_t pin;
  bool stable;
  bool prevStable;
  uint32_t lastEdgeMs;
  uint32_t pressStartMs;
};

Btn btnDot{DOT_BTN_PIN, false, false, 0, 0};
Btn btnDash{DASH_BTN_PIN, false, false, 0, 0};
Btn btnOk{OK_BTN_PIN, false, false, 0, 0};

bool prevAnyPressed = false;
uint32_t lastSilenceStartMs = 0;

// ================= OK multi-click tracking =================
uint8_t okMultiCount = 0;
uint32_t okMultiStartMs = 0;
bool okClearLatched = false;

// ================= Playback state machine =================
// We encode a full "stage" sequence with symbols:
// '.'  = dot tone (1u)
// '-'  = dash tone (3u)
// 'i'  = inter-element gap (1u)
// '|'  = inter-letter gap (3u)
// '/'  = inter-word gap (7u)
bool playActive = false;
String playSequence = "";    // sequence of stage symbols
size_t playIndex = 0;        // current stage index
uint32_t playStageStart = 0; // millis when current stage started
uint16_t playStageDur = 0;   // ms duration of current stage
bool playToneOn = false;     // stage is tone (true) or gap (false)
bool playInLoopGap = false;  // true while we’re in the loop gap

// Keep last committed pattern for single-letter play if needed
String lastCommittedPattern = "";

// ================= Buzzer helpers =================
inline void buzzerOn() { digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LOW ? LOW : HIGH); }
inline void buzzerOff() { digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LOW ? HIGH : LOW); }

// ================= Morse table =================
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

String encodeMorse(char ch)
{
  if (ch >= 'a' && ch <= 'z')
    ch = ch - 'a' + 'A';
  for (size_t i = 0; i < MORSE_TABLE_LEN; i++)
    if (MORSE_TABLE[i].ch == ch)
      return String(MORSE_TABLE[i].pattern);
  return "";
}

// ================= Buffers =================
String currentSymbols = ""; // uncommitted pattern for current letter
String decodedText = "";    // committed text
bool textWasTrimmed = false;

// ================= Utilities =================
inline bool rawPressed(uint8_t pin) { return digitalRead(pin) == LOW; } // buttons active-LOW
inline bool anyPressed() { return btnDot.stable || btnDash.stable; }    // DOT/DASH only

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
  lastCommittedPattern = currentSymbols;
  Serial.printf("LETTER: %s -> %c\n", currentSymbols.c_str(), c);
  currentSymbols = "";
}

void clearAll()
{
  decodedText = "";
  currentSymbols = "";
  lastCommittedPattern = "";
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

// -------- Build play sequence (stages) --------
// Build stages for one Morse pattern (".-" etc.)
String buildStagesForPattern(const String &pat)
{
  String seq;
  for (int i = 0; i < (int)pat.length(); ++i)
  {
    char s = pat[i];
    if (s == '.' || s == '-')
    {
      seq += s; // tone
      if (i < (int)pat.length() - 1)
        seq += 'i'; // inter-element gap (1u) except after last element
    }
  }
  return seq;
}

// Build full play sequence from a text message (letters/spaces)
String buildStagesFromText(const String &msg)
{
  String seq;
  for (int i = 0; i < (int)msg.length(); ++i)
  {
    char ch = msg[i];
    if (ch == ' ')
    {
      // collapse consecutive spaces into one word gap
      if (seq.length() == 0 || seq[seq.length() - 1] == '/')
        continue;
      // replace a trailing letter gap with word gap, if present
      if (seq.length() > 0 && seq[seq.length() - 1] == '|')
        seq.remove(seq.length() - 1);
      seq += '/'; // word gap (7u)
      continue;
    }
    String pat = encodeMorse(ch);
    if (pat.length() == 0)
      continue; // skip unknown chars
    seq += buildStagesForPattern(pat);
    // add inter-letter gap unless next char is space or end
    // look ahead to next non-space valid char
    int j = i + 1;
    while (j < (int)msg.length() && msg[j] == ' ')
      j++;
    if (j < (int)msg.length())
    {
      // next is another letter/number/punct
      seq += '|';
    }
  }
  // remove trailing letter gap if any
  if (seq.length() > 0 && seq[seq.length() - 1] == '|')
    seq.remove(seq.length() - 1);
  return seq;
}

// Build sequence for currentSymbols OR entire decodedText
String buildStagesForPlayback()
{
  if (currentSymbols.length() > 0)
  {
    return buildStagesForPattern(currentSymbols); // play in-progress letter
  }
  // Build from full committed text
  // Trim trailing spaces
  String msg = decodedText;
  while (msg.length() > 0 && msg[msg.length() - 1] == ' ')
    msg.remove(msg.length() - 1);
  return buildStagesFromText(msg);
}

// -------- Playback engine --------
void startStageFromIndex(uint32_t now)
{
  if (playIndex >= playSequence.length())
  {
    // end of message -> loop gap
    playInLoopGap = true;
    playToneOn = false;
    playStageDur = PLAY_LOOP_GAP_MS;
    playStageStart = now;
    buzzerOff();
    return;
  }
  char s = playSequence[playIndex];
  switch (s)
  {
  case '.':
    playToneOn = true;
    playStageDur = PLAY_DOT_MS;
    buzzerOn();
    break;
  case '-':
    playToneOn = true;
    playStageDur = PLAY_DASH_MS;
    buzzerOn();
    break;
  case 'i':
    playToneOn = false;
    playStageDur = PLAY_INTER_GAP_MS;
    buzzerOff();
    break;
  case '|':
    playToneOn = false;
    playStageDur = LETTER_GAP_MS;
    buzzerOff();
    break;
  case '/':
    playToneOn = false;
    playStageDur = WORD_GAP_MS;
    buzzerOff();
    break;
  default:
    playToneOn = false;
    playStageDur = PLAY_INTER_GAP_MS;
    buzzerOff();
    break;
  }
  playStageStart = now;
}

void stopPlayback()
{
  playActive = false;
  buzzerOff();
}

void startPlayback(uint32_t now)
{
  playSequence = buildStagesForPlayback();
  if (playSequence.length() == 0)
  {
    Serial.println("PLAY: NO SEQUENCE");
    return;
  }
  playIndex = 0;
  playActive = true;
  playInLoopGap = false;
  startStageFromIndex(now);
  Serial.print("PLAY START: stages=");
  Serial.println(playSequence);
}

void servicePlayback(uint32_t now)
{
  if (!playActive)
    return;

  // if we're in loop gap and it finished, restart from beginning
  if (playInLoopGap)
  {
    if (now - playStageStart >= playStageDur)
    {
      playInLoopGap = false;
      playIndex = 0;
      startStageFromIndex(now);
    }
    return;
  }

  // advance stage when its time is up
  if (now - playStageStart >= playStageDur)
  {
    playIndex++;
    if (playIndex >= playSequence.length())
    {
      // run loop gap
      playInLoopGap = true;
      playToneOn = false;
      playStageDur = PLAY_LOOP_GAP_MS;
      playStageStart = now;
      buzzerOff();
    }
    else
    {
      startStageFromIndex(now);
    }
  }
}

// ================= OLED UI =================
void drawUI()
{
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  // Header
  display.setCursor(0, 0);
  display.print(playActive ? "ESP32 Morse (PLAYING)" : "ESP32 Morse (3-btn)");

  // Line 2: unit + tag (tag only when playing)
  display.setCursor(0, 10);
  display.print("u=");
  display.print(UNIT_MS);
  display.print("ms");
  if (playActive)
  {
    display.print("  jrcsrg");
  }

  // Line 3: key states
  display.setCursor(0, 22);
  display.print("DOT:");
  display.print(btnDot.stable ? "DOWN" : "UP  ");
  display.setCursor(64, 22);
  display.print("DASH:");
  display.print(btnDash.stable ? "DOWN" : "UP  ");

  // Line 4: show either building letter or a short hint
  display.setCursor(0, 34);
  if (playActive)
  {
    display.print("PLAYING MSG...");
  }
  else
  {
    display.print("Letter: ");
    display.print(currentSymbols);
  }

  // Line 5: decoded tail
  display.setCursor(0, 46);
  display.print("Text:");
  String tail = decodedText;
  bool showEllipsis = textWasTrimmed && tail.length() > OLED_TAIL_CHARS;
  if (tail.length() > OLED_TAIL_CHARS)
    tail = tail.substring(tail.length() - OLED_TAIL_CHARS);
  display.setCursor(0, 56);
  if (showEllipsis)
    display.print("...");
  display.print(tail);

  display.display();
}

// ================= Setup / Loop =================
void setup()
{
  Serial.begin(115200);
  delay(150);

  pinMode(DOT_BTN_PIN, INPUT_PULLUP);
  pinMode(DASH_BTN_PIN, INPUT_PULLUP);
  pinMode(OK_BTN_PIN, INPUT_PULLUP);

  // Silent at boot for active-LOW
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

  // Minimal splash
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.print("ESP32 Morse Ready");
  display.setCursor(0, 12);
  display.print("DOT=13 DASH=14 OK=27");

  display.setCursor(0, 24);
  display.print("JRCSRG 2025");
  display.display();
  delay(2000);
}

void loop()
{
  uint32_t now = millis();

  // Update buttons
  int8_t evDot = updateButton(btnDot, now);
  int8_t evDash = updateButton(btnDash, now);
  int8_t evOk = updateButton(btnOk, now);

  // Cancel playback on any input
  if (playActive && (evDot == +1 || evDash == +1 || evOk == +1))
  {
    stopPlayback();
    Serial.println("PLAY STOP (user input)");
  }

  // Buzzer behavior
  if (playActive)
  {
    servicePlayback(now); // playback drives buzzer
  }
  else
  {
    bool nowAnyPressed = anyPressed();
    if (nowAnyPressed)
      buzzerOn();
    else
      buzzerOff();

    // Optional auto-commit/space based on idle gaps
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
    if (prevAnyPressed && !nowAnyPressed)
      lastSilenceStartMs = now;
    prevAnyPressed = nowAnyPressed;
  }

  // Append symbols on release
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

  // OK long-press = clear
  if (btnOk.stable && !okClearLatched && (now - btnOk.pressStartMs >= CLEAR_HOLD_MS))
  {
    clearAll();
    okClearLatched = true;
    stopPlayback();
    okMultiCount = 0;
  }
  if (!btnOk.stable)
    okClearLatched = false;

  // OK short-press with triple-tap detection
  if (evOk == -1)
  {
    uint32_t held = now - btnOk.pressStartMs;
    if (held < CLEAR_HOLD_MS)
    {
      if (okMultiCount == 0)
      {
        okMultiCount = 1;
        okMultiStartMs = now;
      }
      else if (now - okMultiStartMs <= OK_MULTI_WINDOW_MS)
      {
        okMultiCount++;
      }
      else
      {
        if (okMultiCount < 3)
        {
          commitLetterIfAny();
          Serial.println("OK: COMMIT (timeout)");
        }
        okMultiCount = 1;
        okMultiStartMs = now;
      }

      // Triple tap: start/stop playback of current letter or the whole committed message
      if (okMultiCount >= 3)
      {
        if (playActive)
        {
          stopPlayback();
          Serial.println("PLAY TOGGLE: OFF");
        }
        else
        {
          startPlayback(now); // builds from currentSymbols OR decodedText
          if (playActive)
            Serial.println("PLAY TOGGLE: ON");
        }
        okMultiCount = 0;
      }
    }
  }

  // Commit after single/double tap when window ends
  if (okMultiCount > 0 && (now - okMultiStartMs > OK_MULTI_WINDOW_MS))
  {
    if (okMultiCount < 3)
    {
      commitLetterIfAny();
      Serial.println("OK: COMMIT");
    }
    okMultiCount = 0;
  }

  drawUI();
  delay(5);
}
