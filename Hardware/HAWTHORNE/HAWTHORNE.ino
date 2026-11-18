/* HAWTHORNE - transport controller for Hawthorne (in MELVILLE)  (version: firmware_version.h)
   Hardware is named after the software it controls.
   (converted from the ESP32 DAW Remote v2.2 for REAPER)

   Talks to MELVILLE's OSC control surface ("HAWTHORNE" section of OscControl.h):
     - sends keys to MELVILLE on UDP 8100
     - MELVILLE answers with every lamp and readout on UDP 9100 (to whichever address it heard from)
     - sends /melville/sync every 3 s so MELVILLE keeps answering (and to spot a lost link)
   In MELVILLE: INTERFACE window -> OSC on, listen 8100, reply 9100 (the defaults).

   MELVILLE: PAIRING AND UPDATES
     - every 3 s (with the sync) it introduces itself:  /melville/hello "hawthorne" name serial firmware
     - pairing: MELVILLE asks (/melville/pair, from its DEVICES list); the LCD asks "PAIR MELVILLE?"
       and PLAY says yes (STOP no, or wait 30 s). Yes makes a new key for this box and sends it back
       (/melville/paired serial key); the old one, and any MELVILLE that had it, stops working.
     - updates: MELVILLE posts the firmware to http://<this box>/update with the key in X-Hawthorne-Key.
       The firmware has to be signed with the private key the build keeps (signing_key.h is its public
       half): anything else is refused. It goes into the spare half of the flash, so an update that
       fails or breaks off leaves the old firmware running; the new one only starts if it all went in
       and the signature checked out.
     - the Arduino IDE's network upload is only on in settings mode (GPIO12 held at boot), with this
       box's key as the password (the settings page shows it).

   PANEL (as built: displays on the left, a 3 x 5 key grid on the right, six lit keys along the bottom)

     The 15 grid keys do the old REAPER remote's jobs, Hawthorne's way (scroll view, undo and redo
     had no Hawthorne match, so those keys are new), grouped by what they're for:

       RTZ        LOC 1      END             where it goes
       IN         LOOP       OUT             the loop
       |< MARK    MARK       MARK >|         markers
       SEMI -     CLICK      SEMI +          varispeed, MELVILLE's click
       DEL MARK   STOP       SAVE

     [REW] [FFW] [STOP] [PLAY] [REVERSE] [REC]      the six lit keys
     white white yellow green  blue      red

   KEYS
     LOC 1            tap: go there      hold (0.6 s): store this place in it
     IN / OUT         tap: go there (if it isn't set yet, sets it here)      hold (0.6 s): set it here
     SEMI + / -       varispeed to the next whole semitone up / down
     REW / FFW wind while held.

   DISPLAYS
     6-digit TM1637   tape counter  H.MM.SS.t
     4-digit TM1637   bar.beat  BBB.b  (from MELVILLE's tempo, meter and bar 1)
     16x2 LCD         as on the old REAPER remote:
                      line 1: transport icon (rec / reverse / play / stop), loop icon, tape speed
                              "1.00x", and semitones when the varispeed sits on one ("1.12x +2st")
                      line 2: the current marker (short confirmations and link problems show here
                              for a moment)

   KEY TEST: set KEY_TEST to true, upload, and each key shows its chip and pin on the LCD (and sends
   nothing). Use it to check the grid below matches your wiring, then set it back to false.

   Hardware: WT32-ETH01 (ESP32 with onboard Ethernet)

   Wiring (unchanged from v2.2 except where marked NEW / CHANGED):
     6-digit TM1637: CLK -> GPIO32, DIO -> GPIO33
     4-digit TM1637: CLK -> GPIO2,  DIO -> GPIO4
     1602 LCD (I2C 0x27): SDA -> GPIO14, SCL -> GPIO15

     MCP23017 #1 (0x20): the 15 grid keys, same pins as before (see KEYS table)
       **RESET -> 3.3V (CRITICAL)**
     MCP23017 #2 (0x21): A0 -> 3.3V   **RESET -> 3.3V (CRITICAL)**
       GPA0-GPA5  REW FFW STOP PLAY REVERSE REC   (the six lit keys, same pins as before)
       GPB0-GPB5  their lamps (same pins as before)
       GPB6       LOOP lamp (optional: the LCD shows loop anyway)
       GPB7       not used (was the beat LED)
     MCP23017 #3 (0x22) OPTIONAL, for a later panel with lit locate keys: A1 -> 3.3V
       GPA0-GPA7  LOC 1-8 lamps (lit: that locate holds a place);  GPB0, GPB1  IN, OUT lamps (lit: set)
       Not fitted on the current panel; nothing else needs it.

   SETTINGS MODE: hold GPIO12 to GND during boot -> web page at the IP on the LCD

   The first install is by USB (board "WT32-ETH01 Ethernet Module", partition scheme "Default 4MB
   with spiffs", which has the two app slots updates need); MELVILLE does the rest.

   Version: firmware_version.h  (bump it when the firmware changes: MELVILLE offers an update only
   when the one it carries is newer than the box's)
*/

#include <ETH.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <TM1637Display.h>
#include <NetworkEvents.h>
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>
#include <ESPmDNS.h>
#include <Update.h>
#include "firmware_version.h"
#include "signing_key.h"

// ---------- DEFAULT CONFIG ----------
const char* DEVICE_NAME = "HAWTHORNE";              // what it is (named after the software it controls)
String deviceHostname = "hawthorne";                // this unit on the network: "hawthorne-" + the last four
                                                    // hex digits of its MAC, so several can share a network
const char* DEFAULT_MELVILLE_IP = "192.168.1.111";
const char* DEFAULT_MELVILLE_HOSTNAME = "";
const unsigned int DEFAULT_MELVILLE_PORT = 8100;    // MELVILLE listens here
const unsigned int DEFAULT_DEVICE_PORT = 9100;      // MELVILLE answers here
String deviceKey;                                   // this box's key: MELVILLE needs it to update it (made on first start,
                                                    // and again each time it's paired)
const char* PREFS_NS = "hawthorne-tx";

String melvilleIPStr = DEFAULT_MELVILLE_IP;
String melvilleHostname = DEFAULT_MELVILLE_HOSTNAME;
unsigned int melvillePort = DEFAULT_MELVILLE_PORT;
unsigned int devicePort = DEFAULT_DEVICE_PORT;
bool lcdBacklight = true;

IPAddress melvilleIP;
bool hostnameResolved = false;

// ---------- PINS ----------
const uint8_t CLK1_PIN = 32, DIO1_PIN = 33;         // 6-digit
const uint8_t CLK2_PIN = 2,  DIO2_PIN = 4;          // 4-digit
const uint8_t I2C_SDA = 14,  I2C_SCL = 15;
const uint8_t CONFIG_MODE_PIN = 12;

const uint8_t MCP_ADDR[3] = { 0x20, 0x21, 0x22 };

// ---------- STATE FROM MELVILLE ----------
struct HawthorneState {
  bool play = false, stop = true, rec = false, reverse = false;
  bool loop = false, store = false, in = false, out = false, click = false;
  bool loc[8] = { false };
  String counter = "";                // "HH:MM:SS.t"
  String inText = "--:--", outText = "--:--";   // "MM:SS.t" or "--:--"
  String markerNow = "-", markerNext = "-";
  String beat = "";                   // "bar.beat"
  float speed = 1.0f;                 // tape speed (varispeed), 1 = normal
} hw;

unsigned long lastHeardMs = 0;        // last packet from MELVILLE (0: never)
const unsigned long LINK_TIMEOUT_MS = 7000;
const unsigned long SYNC_INTERVAL_MS = 3000;
unsigned long lastSyncMs = 0;

bool melvilleOnline() { return lastHeardMs != 0 && millis() - lastHeardMs < LINK_TIMEOUT_MS; }

// ---------- KEYS ----------
enum KeyKind : uint8_t { K_PRESS, K_HOLD, K_IN, K_OUT, K_LOC };

struct Key {
  uint8_t chip;          // 0 = MCP #1 (0x20), 1 = MCP #2 (0x21)
  uint8_t pin;           // 0-7 GPA, 8-15 GPB
  KeyKind kind;
  const char* addr;      // OSC address sent on press (K_HOLD: 1 on press, 0 on release)
  const char* label;
  uint8_t loc;           // K_LOC: 1-8
};

// The grid, by position: which MCP #1 pin each of the 15 holes is wired to (top row first, left to
// right). This is read from the old firmware's pin list with the box on its side - if KEY_TEST shows
// a key somewhere else, fix it HERE (the jobs below follow the positions).
const uint8_t GRID_PIN[5][3] = {
  { 14, 5,  12 },
  { 3,  10, 1  },
  { 8,  7,  6  },
  { 13, 4,  11 },
  { 2,  9,  0  },
};
#define AT(row, col) 0, GRID_PIN[row][col]

const Key KEYS[] = {
  // --- the six lit keys, MCP #2 port A (lamps on port B, same bit)
  { 1, 0,  K_HOLD,  "/hawthorne/rew",         "REW",     0 },   // white
  { 1, 1,  K_HOLD,  "/hawthorne/ffw",         "FFW",     0 },   // white
  { 1, 2,  K_PRESS, "/hawthorne/stop",        "STOP",    0 },   // yellow
  { 1, 3,  K_PRESS, "/hawthorne/play",        "PLAY",    0 },   // green
  { 1, 4,  K_PRESS, "/hawthorne/reverse",     "REVERSE", 0 },   // blue
  { 1, 5,  K_PRESS, "/hawthorne/rec",         "REC",     0 },   // red
  // --- the grid, MCP #1, by position (row, column)
  { AT(0, 0), K_PRESS, "/hawthorne/rtz",           "RTZ",      0 },
  { AT(0, 1), K_LOC,   "/hawthorne/locate/1",      "LOC 1",    1 },
  { AT(0, 2), K_PRESS, "/hawthorne/end",           "END",      0 },   // end of what's recorded
  { AT(1, 0), K_IN,    "/hawthorne/in",            "IN",       0 },
  { AT(1, 1), K_PRESS, "/hawthorne/loop",          "LOOP",     0 },
  { AT(1, 2), K_OUT,   "/hawthorne/out",           "OUT",      0 },
  { AT(2, 0), K_PRESS, "/hawthorne/marker/prev",   "|< MARK",  0 },
  { AT(2, 1), K_PRESS, "/hawthorne/mark",          "MARK",     0 },   // add a marker
  { AT(2, 2), K_PRESS, "/hawthorne/marker/next",   "MARK >|",  0 },
  { AT(3, 0), K_PRESS, "/hawthorne/vari/down",     "SEMI -",   0 },   // varispeed -1 semitone
  { AT(3, 1), K_PRESS, "/hawthorne/click",         "CLICK",    0 },   // MELVILLE's click on / off
  { AT(3, 2), K_PRESS, "/hawthorne/vari/up",       "SEMI +",   0 },   // varispeed +1 semitone
  { AT(4, 0), K_PRESS, "/hawthorne/marker/delete", "DEL MARK", 0 },   // the marker it's in
  { AT(4, 1), K_PRESS, "/hawthorne/stop",          "STOP",     0 },
  { AT(4, 2), K_PRESS, "/hawthorne/save",          "SAVE",     0 },   // the reel
};
const uint8_t NUM_KEYS = sizeof(KEYS) / sizeof(KEYS[0]);
const unsigned long DEBOUNCE_MS = 30;
const unsigned long KEY_SCAN_MS = 2;
const unsigned long LONG_PRESS_MS = 600;     // LOC / IN / OUT held this long: store / set
const bool KEY_TEST = false;                 // true: show each key's chip and pin on the LCD, send nothing

bool keyDown[NUM_KEYS] = { false };
bool keyRaw[NUM_KEYS] = { false };
unsigned long keyChangedMs[NUM_KEYS] = { 0 };
unsigned long keyDownMs[NUM_KEYS] = { 0 };
bool keyLongDone[NUM_KEYS] = { false };
unsigned long lastScanMs = 0;

// ---------- LAMPS ----------
// MCP #2 port B: bit n is the lamp in the transport key on GPAn, then LOOP and STORE.
// MCP #3 (optional): port A LOC 1-8, port B bit 0 IN, bit 1 OUT.
uint8_t lastPortB2 = 0xFF, lastPortA3 = 0xFF, lastPortB3 = 0xFF;

// ---------- DISPLAYS ----------
TM1637Display disp6(CLK1_PIN, DIO1_PIN);
TM1637Display disp4(CLK2_PIN, DIO2_PIN);
Adafruit_MCP23X17 mcp[3];
bool mcpOk[3] = { false, false, false };
LiquidCrystal_I2C lcd(0x27, 16, 2);
bool lcdAvailable = false;

#define LCD_CHAR_PLAY     0
#define LCD_CHAR_STOP     1
#define LCD_CHAR_RECORD   2
#define LCD_CHAR_REVERSE  3
#define LCD_CHAR_LOOP     4

// The 6-digit module's digits aren't wired left-to-right: logical position -> segment index.
const uint8_t DISP6_ORDER[6] = { 2, 1, 0, 5, 4, 3 };
const uint8_t SEG_DASH = 0x40;

String flashMsg = "";                // short confirmation on LCD line 2
unsigned long flashUntil = 0;

uint8_t lastSegs6[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t lastSegs4[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t lastLcd[2][16];
bool lcdDirty = true;
unsigned long lastDisplayMs = 0;
const unsigned long DISPLAY_INTERVAL_MS = 20;

// ---------- PAIRING / UPDATING ----------
bool pairAsking = false;                            // MELVILLE asked to pair: PLAY yes, STOP no
unsigned long pairAskedMs = 0;
const unsigned long PAIR_WAIT_MS = 30000;
bool updAuthorised = false, updOk = false;
String updError;
size_t updTotal = 0, updDone = 0;
int updShownPct = -1;

// ---------- NETWORK ----------
WiFiUDP Udp;
uint8_t udpBuf[1536];
WebServer server(80);
Preferences preferences;
static volatile bool eth_connected = false;
bool configMode = false;

// ---------- DECLARATIONS ----------
void sendOSC(const char* address, float value);
void parseOSCPacket(const uint8_t* p, int len, int depth);
void handleOSC(const char* addr, char type, float num, const char* str);
void scanKeys();
void onKeyPress(uint8_t k);
void onKeyRelease(uint8_t k);
void onKeyLong(uint8_t k);
void updateLamps();
void updateDisplays();
void flash(const String& msg);
void createLCDCustomChars();
void setupWebServer();
void handleRoot();
void handleNetworkSave();
void handleLCDSave();
void handleReset();
void handleNewKey();
void newDeviceKey();
void sendHello();
void sendOSCStrings(const char* address, std::initializer_list<const char*> args);
void setupUpdateServer();

// ---------- CONFIG ----------
void loadConfig() {
  preferences.begin(PREFS_NS, true);
  melvilleIPStr    = preferences.getString("mel_ip", DEFAULT_MELVILLE_IP);
  melvilleHostname = preferences.getString("mel_host", DEFAULT_MELVILLE_HOSTNAME);
  melvillePort     = preferences.getUInt("mel_port", DEFAULT_MELVILLE_PORT);
  devicePort       = preferences.getUInt("dev_port", DEFAULT_DEVICE_PORT);
  lcdBacklight     = preferences.getBool("backlight", true);
  deviceKey        = preferences.getString("dev_key", "");
  preferences.end();
  if (deviceKey.length() != 32) newDeviceKey();      // (first start)
}

// A fresh random key (128 bits, as hex), kept in flash. Whoever had the old one can no longer update this box.
void newDeviceKey() {
  char hex[33];
  for (int i = 0; i < 4; i++) snprintf(hex + i * 8, 9, "%08lx", (unsigned long)esp_random());
  deviceKey = hex;
  preferences.begin(PREFS_NS, false);
  preferences.putString("dev_key", deviceKey);
  preferences.end();
}

// (the same time whether or not they match: no clues from how long it took)
static bool sameKey(const String& a, const String& b) {
  if (a.length() != b.length() || a.length() == 0) return false;
  uint8_t diff = 0;
  for (unsigned i = 0; i < a.length(); i++) diff |= (uint8_t)(a.charAt(i) ^ b.charAt(i));
  return diff == 0;
}

void saveNetworkConfig() {
  preferences.begin(PREFS_NS, false);
  preferences.putString("mel_ip", melvilleIPStr);
  preferences.putString("mel_host", melvilleHostname);
  preferences.putUInt("mel_port", melvillePort);
  preferences.putUInt("dev_port", devicePort);
  preferences.end();
}

void saveLCDConfig() {
  preferences.begin(PREFS_NS, false);
  preferences.putBool("backlight", lcdBacklight);
  preferences.end();
}

bool parseIPAddress(const String& s, IPAddress& ip) {
  int parts[4], n = 0;
  String cur = "";
  for (unsigned int i = 0; i <= s.length(); i++) {
    char c = i < s.length() ? s.charAt(i) : '.';
    if (c == '.') {
      if (cur.length() == 0 || cur.length() > 3 || n > 3) return false;
      parts[n++] = cur.toInt();
      cur = "";
    } else if (c >= '0' && c <= '9') {
      cur += c;
    } else {
      return false;
    }
  }
  if (n != 4) return false;
  for (int i = 0; i < 4; i++) if (parts[i] > 255) return false;
  ip = IPAddress(parts[0], parts[1], parts[2], parts[3]);
  return true;
}

bool resolveMelvilleHost() {
  if (melvilleHostname.length() > 0) {
    IPAddress resolved;
    if (WiFi.hostByName(melvilleHostname.c_str(), resolved) == 1) {
      melvilleIP = resolved;
      hostnameResolved = true;
      return true;
    }
  }
  hostnameResolved = false;
  return parseIPAddress(melvilleIPStr, melvilleIP);
}

// ---------- ETHERNET ----------
void onEthEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(deviceHostname.c_str());
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      eth_connected = false;
      break;
    default:
      break;
  }
}

// ---------- OSC OUT ----------
void sendOSC(const char* address, float value) {
  if (!eth_connected) return;
  const int len = strlen(address);
  if (len > 100) return;
  uint8_t buf[128];
  const int padded = (len + 4) & ~3;          // the address and at least one 0, to a multiple of 4
  memset(buf, 0, padded);
  memcpy(buf, address, len);
  int i = padded;
  buf[i++] = ','; buf[i++] = 'f'; buf[i++] = 0; buf[i++] = 0;
  uint32_t u;
  memcpy(&u, &value, 4);
  buf[i++] = (u >> 24) & 0xFF;
  buf[i++] = (u >> 16) & 0xFF;
  buf[i++] = (u >> 8) & 0xFF;
  buf[i++] = u & 0xFF;
  Udp.beginPacket(melvilleIP, melvillePort);
  Udp.write(buf, i);
  Udp.endPacket();
}

// An OSC message of strings (hello, paired).
void sendOSCStrings(const char* address, std::initializer_list<const char*> args) {
  if (!eth_connected) return;
  uint8_t buf[256];
  int i = 0;
  auto put = [&](const char* str) -> bool {
    const int len = strlen(str);
    const int padded = (len + 4) & ~3;
    if (i + padded > (int)sizeof(buf)) return false;
    memset(buf + i, 0, padded);
    memcpy(buf + i, str, len);
    i += padded;
    return true;
  };
  char tags[12] = ",";
  int n = 1;
  for (size_t k = 0; k < args.size() && n < 11; k++) tags[n++] = 's';
  tags[n] = 0;
  if (!put(address) || !put(tags)) return;
  for (const char* a : args) if (!put(a)) return;
  Udp.beginPacket(melvilleIP, melvillePort);
  Udp.write(buf, i);
  Udp.endPacket();
}

// Who this is: MELVILLE's DEVICES list shows it, with its firmware.
void sendHello() {
  sendOSCStrings("/melville/hello", { "hawthorne", DEVICE_NAME, deviceHostname.c_str(), FIRMWARE_VERSION });
}

// ---------- OSC IN ----------
static uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// A 0-terminated, 4-padded OSC string at p; returns the offset after it, or -1.
static int oscString(const uint8_t* p, int len, int at) {
  int i = at;
  while (i < len && p[i] != 0) i++;
  if (i >= len) return -1;
  int next = at + (((i - at) + 4) & ~3);
  return next <= len ? next : -1;
}

void parseOSCPacket(const uint8_t* p, int len, int depth) {
  if (len < 4 || depth > 3) return;

  if (len >= 16 && memcmp(p, "#bundle", 8) == 0) {
    int i = 16;
    while (i + 4 <= len) {
      int32_t size = (int32_t)be32(p + i);
      i += 4;
      if (size <= 0 || i + size > len) return;
      parseOSCPacket(p + i, size, depth + 1);
      i += size;
    }
    return;
  }

  if (p[0] != '/') return;
  int i = oscString(p, len, 0);
  if (i < 0) return;
  const char* addr = (const char*)p;

  if (i >= len || p[i] != ',') { handleOSC(addr, 0, 1.0f, nullptr); return; }
  int tagsAt = i;
  i = oscString(p, len, i);
  if (i < 0) return;
  char type = (char)p[tagsAt + 1];

  if (type == 'f' && i + 4 <= len) {
    uint32_t u = be32(p + i);
    float f;
    memcpy(&f, &u, 4);
    handleOSC(addr, 'f', f, nullptr);
  } else if (type == 'i' && i + 4 <= len) {
    handleOSC(addr, 'f', (float)(int32_t)be32(p + i), nullptr);
  } else if (type == 'T' || type == 'F') {
    handleOSC(addr, 'f', type == 'T' ? 1.0f : 0.0f, nullptr);
  } else if (type == 's') {
    if (oscString(p, len, i) < 0) return;      // (it's 0-terminated inside the packet)
    handleOSC(addr, 's', 0.0f, (const char*)(p + i));
  }
}

struct LampMap { const char* addr; bool* lamp; };

void handleOSC(const char* addr, char type, float num, const char* str) {
  lastHeardMs = millis();
  if (lastHeardMs == 0) lastHeardMs = 1;

  if (strcmp(addr, "/melville/pair") == 0) {             // MELVILLE wants to pair: ask on the box
    pairAsking = true;
    pairAskedMs = millis();
    lcdDirty = true;
    return;
  }
  if (strncmp(addr, "/hawthorne/", 11) != 0) return;   // (TWAIN and /transport/: not ours)
  const char* key = addr + 11;

  if (type == 'f') {
    const bool on = num > 0.5f;
    static const LampMap lamps[] = {
      { "play", &hw.play }, { "stop", &hw.stop }, { "rec", &hw.rec }, { "reverse", &hw.reverse },
      { "loop", &hw.loop }, { "store", &hw.store }, { "in", &hw.in }, { "out", &hw.out }, { "click", &hw.click },
    };
    for (auto& l : lamps) {
      if (strcmp(key, l.addr) == 0) {
        if (*l.lamp != on) { *l.lamp = on; lcdDirty = true; }
        return;
      }
    }
    if (strcmp(key, "speed") == 0) {
      if (num > 0.05f && num < 10.0f && num != hw.speed) { hw.speed = num; lcdDirty = true; }
      return;
    }
    if (strncmp(key, "locate/", 7) == 0) {
      int n = atoi(key + 7);
      if (n >= 1 && n <= 8) hw.loc[n - 1] = on;
    }
    return;
  }

  if (type == 's' && str) {
    String* target = nullptr;
    if      (strcmp(key, "counter") == 0)     target = &hw.counter;
    else if (strcmp(key, "in/text") == 0)     target = &hw.inText;
    else if (strcmp(key, "out/text") == 0)    target = &hw.outText;
    else if (strcmp(key, "marker/now") == 0)  target = &hw.markerNow;
    else if (strcmp(key, "marker/next") == 0) target = &hw.markerNext;
    else if (strcmp(key, "beat") == 0)        target = &hw.beat;
    if (target && *target != str) { *target = str; lcdDirty = true; }
  }
}

// ---------- KEYS ----------
void scanKeys() {
  const unsigned long now = millis();
  if (now - lastScanMs < KEY_SCAN_MS) return;
  lastScanMs = now;

  uint16_t port[2] = { 0xFFFF, 0xFFFF };
  for (uint8_t c = 0; c < 2; c++) if (mcpOk[c]) port[c] = mcp[c].readGPIOAB();

  for (uint8_t k = 0; k < NUM_KEYS; k++) {
    if (!mcpOk[KEYS[k].chip]) continue;
    const bool pressed = !(port[KEYS[k].chip] & (1u << KEYS[k].pin));   // (pulled up: low is pressed)
    if (pressed != keyRaw[k]) { keyRaw[k] = pressed; keyChangedMs[k] = now; }
    if (pressed != keyDown[k] && now - keyChangedMs[k] >= DEBOUNCE_MS) {
      keyDown[k] = pressed;
      if (pressed) onKeyPress(k); else onKeyRelease(k);
    }
    if (keyDown[k] && !keyLongDone[k] && keyDownMs[k] && now - keyDownMs[k] >= LONG_PRESS_MS) {
      keyLongDone[k] = true;
      onKeyLong(k);
    }
  }
}

static String pinName(uint8_t chip, uint8_t pin);

void onKeyPress(uint8_t k) {
  const Key& key = KEYS[k];
  keyDownMs[k] = millis();
  if (keyDownMs[k] == 0) keyDownMs[k] = 1;
  keyLongDone[k] = false;

  if (KEY_TEST) {
    flash(String(key.label) + " " + pinName(key.chip, key.pin));   // (the label is where the firmware thinks it is)
    return;
  }
  if (pairAsking) {                                    // PLAY: yes; STOP: no; the rest wait
    keyLongDone[k] = true;                             // (so its release does nothing either)
    if (strcmp(key.addr, "/hawthorne/play") == 0) {
      pairAsking = false;
      newDeviceKey();
      sendOSCStrings("/melville/paired", { deviceHostname.c_str(), deviceKey.c_str() });
      flash("PAIRED");
    } else if (strcmp(key.addr, "/hawthorne/stop") == 0) {
      pairAsking = false;
      sendOSCStrings("/melville/pair/declined", { deviceHostname.c_str() });
      flash("NOT PAIRED");
    }
    lcdDirty = true;
    return;
  }
  // (LOC / IN / OUT wait for the release or the hold: a tap goes, a hold stores)
  if (key.kind == K_HOLD || key.kind == K_PRESS) sendOSC(key.addr, 1.0f);
  if (strcmp(key.addr, "/hawthorne/save") == 0)               flash("REEL SAVED");
  else if (strcmp(key.addr, "/hawthorne/mark") == 0)          flash("MARKER ADDED");
  else if (strcmp(key.addr, "/hawthorne/marker/delete") == 0) flash(hw.markerNow == "-" ? String("NO MARKER HERE") : "DELETED " + hw.markerNow);
  else if (strcmp(key.addr, "/hawthorne/click") == 0)         flash(hw.click ? "CLICK OFF" : "CLICK ON");
  lcdDirty = true;
}

// held LONG_PRESS_MS: store / set here
void onKeyLong(uint8_t k) {
  if (KEY_TEST) return;
  const Key& key = KEYS[k];
  if (key.kind == K_LOC) {
    sendOSC(("/hawthorne/store/" + String(key.loc)).c_str(), 1.0f);
    hw.loc[key.loc - 1] = true;
    flash("LOC " + String(key.loc) + " STORED");
  } else if (key.kind == K_IN || key.kind == K_OUT) {
    const bool isIn = key.kind == K_IN;
    sendOSC(isIn ? "/hawthorne/in" : "/hawthorne/out", 1.0f);
    flash(isIn ? "IN SET" : "OUT SET");
  }
}

void onKeyRelease(uint8_t k) {
  const Key& key = KEYS[k];
  const bool wasLong = keyLongDone[k];             // (also set by a key that answered the pairing question)
  keyDownMs[k] = 0;
  keyLongDone[k] = false;
  if (KEY_TEST) return;

  if (key.kind == K_HOLD) { sendOSC(key.addr, 0.0f); return; }   // (stop winding)
  if (wasLong) return;                                           // (the hold already did it)

  if (key.kind == K_LOC) {
    if (!hw.loc[key.loc - 1]) flash("LOC " + String(key.loc) + " EMPTY");
    sendOSC(key.addr, 1.0f);
  } else if (key.kind == K_IN || key.kind == K_OUT) {
    const bool isIn = key.kind == K_IN;
    if (isIn ? hw.in : hw.out) {
      sendOSC(isIn ? "/hawthorne/goto/in" : "/hawthorne/goto/out", 1.0f);
    } else {                                                     // (nothing there yet: set it)
      sendOSC(isIn ? "/hawthorne/in" : "/hawthorne/out", 1.0f);
      flash(isIn ? "IN SET" : "OUT SET");
    }
  }
}

// ---------- LAMPS ----------
bool keyHeld(const char* addr) {
  for (uint8_t k = 0; k < NUM_KEYS; k++) if (keyDown[k] && strcmp(KEYS[k].addr, addr) == 0) return true;
  return false;
}

void updateLamps() {
  if (mcpOk[1]) {
    uint8_t b = 0;
    if (keyHeld("/hawthorne/rew")) b |= 1 << 0;      // (MELVILLE doesn't light winding: lit while held)
    if (keyHeld("/hawthorne/ffw")) b |= 1 << 1;
    if (hw.stop)    b |= 1 << 2;
    if (hw.play)    b |= 1 << 3;
    if (hw.reverse) b |= 1 << 4;
    if (hw.rec)     b |= 1 << 5;
    if (hw.loop)    b |= 1 << 6;
    if (!melvilleOnline()) b = ((millis() / 500) % 2 == 0) ? (1 << 2) : 0;   // (no link: STOP blinks)
    if (b != lastPortB2) { mcp[1].writeGPIOB(b); lastPortB2 = b; }
  }
  if (mcpOk[2]) {
    uint8_t a = 0, b = 0;
    for (int i = 0; i < 8; i++) if (hw.loc[i]) a |= 1 << i;
    if (hw.in)  b |= 1 << 0;
    if (hw.out) b |= 1 << 1;
    if (a != lastPortA3) { mcp[2].writeGPIOA(a); lastPortA3 = a; }
    if (b != lastPortB3) { mcp[2].writeGPIOB(b); lastPortB3 = b; }
  }
}

// ---------- DISPLAYS ----------
void flash(const String& msg) {
  flashMsg = msg;
  flashUntil = millis() + 1500;
  lcdDirty = true;
}

// "HH:MM:SS.t" -> H.MM.SS.t on the 6-digit
void showCounter() {
  uint8_t logical[6];
  int h, m, s, t;
  if (hw.counter.length() > 0 && sscanf(hw.counter.c_str(), "%d:%d:%d.%d", &h, &m, &s, &t) == 4) {
    logical[0] = disp6.encodeDigit(h % 10) | 0x80;
    logical[1] = disp6.encodeDigit((m / 10) % 10);
    logical[2] = disp6.encodeDigit(m % 10) | 0x80;
    logical[3] = disp6.encodeDigit((s / 10) % 10);
    logical[4] = disp6.encodeDigit(s % 10) | 0x80;
    logical[5] = disp6.encodeDigit(t % 10);
  } else {
    for (int i = 0; i < 6; i++) logical[i] = SEG_DASH;
  }
  uint8_t segs[6];
  for (int i = 0; i < 6; i++) segs[DISP6_ORDER[i]] = logical[i];
  if (memcmp(segs, lastSegs6, 6) != 0) {
    disp6.setSegments(segs, 6);
    memcpy(lastSegs6, segs, 6);
  }
}

// "bar.beat" -> BBB.b on the 4-digit (as the old REAPER remote: leading zeros, a point after the bar)
void showBarBeat() {
  uint8_t segs[4];
  long bar;
  int beat;
  if (hw.beat.length() > 0 && sscanf(hw.beat.c_str(), "%ld.%d", &bar, &beat) == 2) {
    if (bar > 999) bar = 999;
    if (beat < 0) beat = 0;
    if (bar < 0) {                                    // (before bar 1: "-1.4" -> -01.4)
      long b = -bar > 99 ? 99 : -bar;
      segs[0] = SEG_DASH;
      segs[1] = disp4.encodeDigit((b / 10) % 10);
      segs[2] = disp4.encodeDigit(b % 10) | 0x80;
    } else {
      segs[0] = disp4.encodeDigit((bar / 100) % 10);
      segs[1] = disp4.encodeDigit((bar / 10) % 10);
      segs[2] = disp4.encodeDigit(bar % 10) | 0x80;
    }
    segs[3] = disp4.encodeDigit(beat % 10);
  } else {
    for (int i = 0; i < 4; i++) segs[i] = SEG_DASH;
  }
  if (memcmp(segs, lastSegs4, 4) != 0) {
    disp4.setSegments(segs, 4);
    memcpy(lastSegs4, segs, 4);
  }
}

static void fillText(uint8_t* line, int from, const String& s) {
  for (int i = from; i < 16; i++) {
    int j = i - from;
    line[i] = j < (int)s.length() ? (uint8_t)s.charAt(j) : ' ';
  }
}

void showLCD() {
  if (!lcdAvailable) return;
  if (flashUntil && millis() > flashUntil) { flashUntil = 0; flashMsg = ""; lcdDirty = true; }

  uint8_t lines[2][16];

  if (pairAsking) {                                    // MELVILLE asked to pair: the question, until it's answered
    fillText(lines[0], 0, "PAIR MELVILLE?");
    fillText(lines[1], 0, "PLAY=YES STOP=NO");
    for (int r = 0; r < 2; r++) {
      if (memcmp(lines[r], lastLcd[r], 16) == 0) continue;
      lcd.setCursor(0, r);
      for (int i = 0; i < 16; i++) lcd.write(lines[r][i]);
      memcpy(lastLcd[r], lines[r], 16);
    }
    return;
  }

  // line 1 (as the old REAPER remote): transport icon, loop icon, speed, semitones
  lines[0][0] = hw.rec ? LCD_CHAR_RECORD : hw.reverse ? LCD_CHAR_REVERSE : hw.play ? LCD_CHAR_PLAY : LCD_CHAR_STOP;
  lines[0][1] = hw.loop ? LCD_CHAR_LOOP : ' ';
  lines[0][2] = ' ';
  char speedBuf[8];
  snprintf(speedBuf, sizeof(speedBuf), "%.2fx", hw.speed);
  String l1 = speedBuf;
  if (hw.speed > 0.0f && fabsf(hw.speed - 1.0f) > 0.01f) {      // (on a semitone: say which)
    const float semitones = 12.0f * logf(hw.speed) / logf(2.0f);
    const int st = (int)roundf(semitones);
    if (st != 0 && fabsf(hw.speed - powf(2.0f, st / 12.0f)) < 0.005f)
      l1 += String(" ") + (st > 0 ? "+" : "") + String(st) + "st";
  }
  fillText(lines[0], 3, l1);

  // line 2: the current marker (a confirmation or a link problem takes it for a moment)
  String l2;
  if (!eth_connected)             l2 = "NO ETHERNET";
  else if (!melvilleOnline())     l2 = "NO MELVILLE";
  else if (flashMsg.length())     l2 = flashMsg;
  else if (hw.store)              l2 = "STORE: PICK LOC";
  else                            l2 = hw.markerNow == "-" ? String("") : hw.markerNow;
  fillText(lines[1], 0, l2);

  for (int r = 0; r < 2; r++) {
    if (memcmp(lines[r], lastLcd[r], 16) == 0) continue;
    lcd.setCursor(0, r);
    for (int i = 0; i < 16; i++) lcd.write(lines[r][i]);
    memcpy(lastLcd[r], lines[r], 16);
  }
}

void updateDisplays() {
  const unsigned long now = millis();
  if (now - lastDisplayMs < DISPLAY_INTERVAL_MS) return;
  lastDisplayMs = now;
  showCounter();
  showBarBeat();
  showLCD();
}

void createLCDCustomChars() {
  byte playChar[8]    = { 0b00000, 0b01000, 0b01100, 0b01110, 0b01100, 0b01000, 0b00000, 0b00000 };
  byte stopChar[8]    = { 0b00000, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b00000, 0b00000 };
  byte recordChar[8]  = { 0b00000, 0b01110, 0b11111, 0b11111, 0b11111, 0b01110, 0b00000, 0b00000 };
  byte reverseChar[8] = { 0b00000, 0b00010, 0b00110, 0b01110, 0b00110, 0b00010, 0b00000, 0b00000 };
  byte loopChar[8]    = { 0b00000, 0b01110, 0b10001, 0b10011, 0b11001, 0b10001, 0b01110, 0b00000 };
  lcd.createChar(LCD_CHAR_PLAY, playChar);
  lcd.createChar(LCD_CHAR_STOP, stopChar);
  lcd.createChar(LCD_CHAR_RECORD, recordChar);
  lcd.createChar(LCD_CHAR_REVERSE, reverseChar);
  lcd.createChar(LCD_CHAR_LOOP, loopChar);
}

void lcdTwoLines(const String& a, const String& b) {
  if (!lcdAvailable) return;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(a.substring(0, 16));
  lcd.setCursor(0, 1); lcd.print(b.substring(0, 16));
  memset(lastLcd, 0xFF, sizeof(lastLcd));       // (the run-time screen redraws from scratch)
}

// ---------- SETUP HELPERS ----------
bool checkConfigMode() {
  pinMode(CONFIG_MODE_PIN, INPUT_PULLUP);
  delay(10);
  if (digitalRead(CONFIG_MODE_PIN)) return false;

  lcdTwoLines("CONFIG MODE", "Connecting...");
  for (int i = 0; i < 3; i++) {
    disp4.setBrightness(0x0f); disp6.setBrightness(0x0f);
    disp4.showNumberDec(0, true); disp6.showNumberDec(0, true);
    delay(200);
    disp4.clear(); disp6.clear();
    delay(200);
  }
  return true;
}

void setupMCPs() {
  for (uint8_t c = 0; c < 3; c++) mcpOk[c] = mcp[c].begin_I2C(MCP_ADDR[c], &Wire);

  if (mcpOk[0]) for (uint8_t p = 0; p < 16; p++) mcp[0].pinMode(p, INPUT_PULLUP);
  if (mcpOk[1]) {
    for (uint8_t p = 0; p < 8; p++) mcp[1].pinMode(p, INPUT_PULLUP);     // port A: keys
    for (uint8_t p = 8; p < 16; p++) mcp[1].pinMode(p, OUTPUT);          // port B: lamps
    mcp[1].writeGPIOB(0);
  }
  if (mcpOk[2]) {
    for (uint8_t p = 0; p < 16; p++) mcp[2].pinMode(p, OUTPUT);
    mcp[2].writeGPIOA(0);
    mcp[2].writeGPIOB(0);
  }

  delay(50);
  // (whatever is held at boot doesn't count as a press)
  for (uint8_t k = 0; k < NUM_KEYS; k++) {
    if (!mcpOk[KEYS[k].chip]) continue;
    keyRaw[k] = keyDown[k] = !mcp[KEYS[k].chip].digitalRead(KEYS[k].pin);
    keyChangedMs[k] = millis();
  }

  // lamp test
  if (mcpOk[1]) {
    for (uint8_t i = 0; i < 7; i++) { mcp[1].writeGPIOB((2 << i) - 1); delay(80); }
  }
  if (mcpOk[2]) {
    mcp[2].writeGPIOA(0xFF); mcp[2].writeGPIOB(0x03); delay(300);
    mcp[2].writeGPIOA(0); mcp[2].writeGPIOB(0);
  }
  delay(200);
  if (mcpOk[1]) mcp[1].writeGPIOB(0);
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(300);

  disp6.setBrightness(0x0f);
  disp4.setBrightness(0x0f);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  delay(50);
  lcd.init();
  lcd.backlight();
  createLCDCustomChars();
  lcdAvailable = true;
  {
    const uint64_t mac = ESP.getEfuseMac();
    char h[24];
    snprintf(h, sizeof(h), "hawthorne-%02x%02x", (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
    deviceHostname = h;
  }
  lcdTwoLines(DEVICE_NAME, "TRANSPORT");
  delay(1000);

  configMode = checkConfigMode();
  loadConfig();

  setupMCPs();
  if (!mcpOk[0] || !mcpOk[1]) {
    lcdTwoLines("MCP ERROR!", String(mcpOk[0] ? "" : "0x20 ") + (mcpOk[1] ? "" : "0x21"));
    if (!configMode) while (1) delay(100);
    delay(2000);
  }

  Network.onEvent(onEthEvent);
  ETH.begin(ETH_PHY_LAN8720, 1, 23, 18, 16, ETH_CLOCK_GPIO0_IN);
  ETH.setHostname(deviceHostname.c_str());

  lcdTwoLines(DEVICE_NAME, "Wait Ethernet...");
  while (!eth_connected) delay(250);

  if (MDNS.begin(deviceHostname.c_str())) MDNS.addService("osc", "udp", devicePort);

  if (configMode) {
    lcdTwoLines("Config Mode", ETH.localIP().toString());
    setupWebServer();
    // the Arduino IDE's network upload: only here (someone at the box held GPIO12), with the box's key
    ArduinoOTA.setHostname(deviceHostname.c_str());
    ArduinoOTA.setPassword(deviceKey.c_str());
    ArduinoOTA.begin();
    return;
  }

  lcdTwoLines("Resolving...", melvilleHostname.length() ? melvilleHostname : melvilleIPStr);
  resolveMelvilleHost();

  lcdTwoLines(deviceHostname, ETH.localIP().toString());
  delay(3000);
  lcdTwoLines("> MELVILLE", melvilleIP.toString() + ":" + String(melvillePort));
  delay(1500);

  if (!lcdBacklight) lcd.noBacklight();

  Udp.begin(devicePort);
  delay(100);
  setupUpdateServer();                             // (MELVILLE's firmware updates)

  lcd.clear();
  memset(lastLcd, 0xFF, sizeof(lastLcd));
  sendHello();
  sendOSC("/melville/sync", 1.0f);               // (MELVILLE answers with every lamp and readout)
  lastSyncMs = millis();
}

// ---------- LOOP ----------
void loop() {
  if (configMode) {
    server.handleClient();
    ArduinoOTA.handle();
    return;
  }

  server.handleClient();                          // (a firmware update from MELVILLE)
  if (pairAsking && millis() - pairAskedMs > PAIR_WAIT_MS) {   // (no answer: no)
    pairAsking = false;
    sendOSCStrings("/melville/pair/declined", { deviceHostname.c_str() });
    flash("NOT PAIRED");
  }

  for (int n = 0; n < 16; n++) {                  // (a sync answer is ~40 small packets)
    int size = Udp.parsePacket();
    if (size <= 0) break;
    if (size > (int)sizeof(udpBuf)) size = sizeof(udpBuf);
    int got = Udp.read(udpBuf, size);
    if (got > 0) parseOSCPacket(udpBuf, got, 0);
  }

  scanKeys();

  const unsigned long now = millis();
  if (now - lastSyncMs >= SYNC_INTERVAL_MS) {
    // (if MELVILLE was found by hostname and has gone quiet, look it up again: its address may have changed)
    if (hostnameResolved && !melvilleOnline() && lastHeardMs != 0) resolveMelvilleHost();
    sendHello();
    sendOSC("/melville/sync", 1.0f);
    lastSyncMs = now;
  }

  updateLamps();
  updateDisplays();
}

// ---------- FIRMWARE UPDATES (from MELVILLE) ----------
// POST /update, the signed firmware as the body, this box's key in X-Hawthorne-Key. It's written to the
// spare app slot as it arrives; the box switches to it only if all of it went in and its signature
// checks out against signing_key.h. GET / says what this is.

#ifdef UPDATE_SIGN
static UpdaterRSAVerifier firmwareSignature(PUBLIC_KEY, PUBLIC_KEY_LEN, HASH_SHA256);
#endif

void showUpdate(const char* line1, int pct) {
  if (!lcdAvailable) return;
  char l2[17];
  if (pct >= 0) snprintf(l2, sizeof(l2), "%3d%%  %-10s", pct, FIRMWARE_VERSION);
  else snprintf(l2, sizeof(l2), "%-16s", "");
  lcdTwoLines(line1, l2);
  // (the counter shows it too)
  if (pct >= 0) {
    disp4.showNumberDec(pct, false);
    uint8_t dashes[6] = { SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH, SEG_DASH };
    disp6.setSegments(dashes, 6);
  }
}

void updateBody() {
  HTTPRaw& raw = server.raw();
  if (raw.status == RAW_START) {
    updOk = false;
    updError = "";
    updDone = 0;
    updShownPct = -1;
    updAuthorised = sameKey(server.header("X-Hawthorne-Key"), deviceKey);
    if (!updAuthorised) { updError = "not paired with this MELVILLE: pair it again"; return; }
    updTotal = (size_t)server.clientContentLength();
#ifdef UPDATE_SIGN
    if (!Update.installSignature(&firmwareSignature)) { updError = "couldn't check signatures"; updAuthorised = false; return; }
#endif
    if (!Update.begin(updTotal)) { updError = Update.errorString(); updAuthorised = false; return; }
    showUpdate("UPDATING...", 0);
  } else if (raw.status == RAW_WRITE) {
    if (!updAuthorised || updError.length()) return;
    if (Update.write(raw.buf, raw.currentSize) != raw.currentSize) {
      updError = Update.errorString();
      Update.abort();
      return;
    }
    updDone += raw.currentSize;
    const int pct = updTotal ? (int)(updDone * 100 / updTotal) : 0;
    if (pct != updShownPct && pct % 5 == 0) { updShownPct = pct; showUpdate("UPDATING...", pct); }
  } else if (raw.status == RAW_END) {
    if (!updAuthorised || updError.length()) return;
    if (Update.end(true)) updOk = true;                // (the signature is checked here)
    else updError = Update.getError() == UPDATE_ERROR_SIGN ? String("not signed by MELVILLE's build: refused")
                                                           : String(Update.errorString());
  } else if (raw.status == RAW_ABORTED) {
    if (updAuthorised && updError.isEmpty()) Update.abort();
    updError = "the upload broke off";
  }
}

void updateDone() {
  if (updOk) {
    server.send(200, "text/plain", "OK");
    showUpdate("UPDATED", 100);
    lcdTwoLines("UPDATED", "RESTARTING...");
    delay(800);
    ESP.restart();
    return;
  }
  server.send(updAuthorised ? 500 : 403, "text/plain", updError.length() ? updError : String("nothing sent"));
  lcd.clear();
  memset(lastLcd, 0xFF, sizeof(lastLcd));
  memset(lastSegs4, 0xFF, sizeof(lastSegs4));
  memset(lastSegs6, 0xFF, sizeof(lastSegs6));
  if (updAuthorised) flash("UPDATE FAILED");
}

void setupUpdateServer() {
  static const char* headers[] = { "X-Hawthorne-Key" };
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, [] {
    server.send(200, "application/json", String("{\"type\":\"hawthorne\",\"serial\":\"") + deviceHostname
                                             + "\",\"firmware\":\"" + FIRMWARE_VERSION + "\"}");
  });
  server.on("/update", HTTP_POST, updateDone, updateBody);
  server.begin();
}

// ---------- WEB SERVER (settings mode) ----------
void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/network", HTTP_POST, handleNetworkSave);
  server.on("/lcd", HTTP_POST, handleLCDSave);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/newkey", HTTP_POST, handleNewKey);
  server.begin();
}

static String htmlEscape(const String& s) {
  String o;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;";
    else if (c == '\'') o += "&#39;"; else if (c == '"') o += "&quot;"; else o += c;
  }
  return o;
}

static String pinName(uint8_t chip, uint8_t pin) {
  return String("MCP") + String(chip + 1) + " " + (pin < 8 ? "GPA" : "GPB") + String(pin % 8);
}

void handleRoot() {
  String html;
  html.reserve(9000);
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>[HAWTHORNE] Transport Controller</title><style>";
  // PEQUOD style - warm amber palette
  html += "*{box-sizing:border-box}";
  html += "body{font-family:Roboto,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;max-width:900px;margin:0 auto;padding:2rem;background:#1a1a1a;color:#e8e8e8}";
  html += "h1{font-family:'Courier New',Courier,monospace;font-size:2.5rem;color:#f4b942;margin-bottom:.5rem;letter-spacing:.1em;text-transform:uppercase;border-bottom:2px solid #8b6f47;padding-bottom:1rem}";
  html += "h2{font-family:'Courier New',Courier,monospace;font-size:1.3rem;color:#f4b942;margin-top:3rem;margin-bottom:1.5rem;letter-spacing:.05em;text-transform:uppercase;border-bottom:2px solid #8b6f47;padding-bottom:.5rem}";
  html += "h3{font-family:'Courier New',Courier,monospace;font-size:1rem;margin-bottom:1rem;letter-spacing:.05em;text-transform:uppercase}";
  html += ".card{background:#8b6f47;padding:1.5rem;margin:1rem 0;border-radius:4px;border:1px solid #a07f57;border-left:4px solid #f4b942}";
  html += "label{display:block;margin:1rem 0 .5rem;color:#e8e8e8;font-size:.95rem}";
  html += "input{width:100%;padding:.75rem 1rem;background:#252525;border:1px solid #8b6f47;color:#e8e8e8;border-radius:4px;font-size:.95rem}";
  html += "input:focus{outline:none;border:2px solid #f4b942}input:disabled{background:#222;color:#8b6f47}";
  html += ".btn{font-family:'Courier New',Courier,monospace;background:#f4b942;color:#1a1a1a;padding:.75rem 2rem;border:none;cursor:pointer;font-size:.9rem;font-weight:bold;letter-spacing:.1em;text-transform:uppercase;border-radius:4px;margin:1rem .5rem 1rem 0}";
  html += ".btn:hover{filter:brightness(1.1)}.btn-danger{background:#e3a391}";
  html += ".help{color:#d8c8a8;font-size:.85rem;margin:.5rem 0 1rem;line-height:1.5}";
  html += "code{font-family:'Courier New',Courier,monospace;background:#252525;padding:.2rem .5rem;border-radius:3px;color:#f4b942}";
  html += "table{width:100%;border-collapse:collapse;font-family:'Courier New',Courier,monospace;font-size:.85rem}";
  html += "th,td{text-align:left;padding:.45rem .6rem;border-bottom:1px solid #3a3a3a}th{color:#f4b942}";
  html += ".version{font-family:'Courier New',Courier,monospace;color:#8b6f47;font-size:.85rem;text-align:center;margin-top:3rem}";
  html += "</style></head><body>";

  html += "<h1>[HAWTHORNE]</h1>";
  html += "<div style='font-family:\"Courier New\",monospace;color:#b0b0b0;margin-bottom:2rem'>Transport controller for Hawthorne &middot; via MELVILLE</div>";

  // Network
  html += "<h2>MELVILLE Connection</h2><form onsubmit='return post(event,\"/network\")'>";
  html += "<div class='card'><h3>MELVILLE</h3>";
  html += "<label>MELVILLE PC IP address:</label>";
  html += "<input type='text' name='mel_ip' value='" + htmlEscape(melvilleIPStr) + "' maxlength='15' required>";
  html += "<label>MELVILLE PC hostname (optional):</label>";
  html += "<input type='text' name='mel_host' value='" + htmlEscape(melvilleHostname) + "' maxlength='64' placeholder='studio-pc.local'>";
  html += "<p class='help'>Blank: use the IP address. If the name can't be found at boot, the IP is used.</p>";
  html += "<label>MELVILLE OSC listen port:</label>";
  html += "<input type='number' name='mel_port' value='" + String(melvillePort) + "' min='1' max='65535' required>";
  html += "<label>Reply port (this device listens here):</label>";
  html += "<input type='number' name='dev_port' value='" + String(devicePort) + "' min='1' max='65535' required>";
  html += "<p class='help'>These must match MELVILLE's INTERFACE window &rarr; OSC: listen <code>8100</code>, reply <code>9100</code> by default.</p>";
  html += "</div><div class='card'><h3>This device</h3>";
  String lc = deviceHostname;
  html += "<label>Hostname (fixed, from this unit's MAC):</label><input type='text' value='" + deviceHostname + "' disabled>";
  html += "<p class='help'>mDNS: <code>" + lc + ".local</code> &middot; IP: <code>" + ETH.localIP().toString() + "</code></p></div>";
  html += "<button type='submit' class='btn'>Save Connection</button></form>";

  // Display
  html += "<h2>Display</h2><form onsubmit='return post(event,\"/lcd\")'><div class='card'>";
  html += "<label style='display:flex;align-items:center;gap:10px;cursor:pointer'>";
  html += "<input type='checkbox' id='backlight' style='width:auto' " + String(lcdBacklight ? "checked" : "") + ">";
  html += "<span>LCD backlight on</span></label>";
  html += "<p class='help'>The I2C backpack only switches the backlight on or off.</p></div>";
  html += "<button type='submit' class='btn'>Save Display</button></form>";

  // Key map
  // Pairing and firmware
  html += "<h2>Pairing &amp; Firmware</h2><div class='card'>";
  html += "<label>Firmware:</label><input type='text' value='" + String(FIRMWARE_VERSION) + "' disabled>";
  html += "<label>This box's key:</label><input type='text' value='" + deviceKey + "' disabled style='font-family:\"Courier New\",monospace'>";
  html += "<p class='help'>MELVILLE gets this key when you pair the box from its DEVICES list, and needs it to update the firmware. "
          "It's also the password for the Arduino IDE's network upload, which only works in this settings mode.</p>";
  html += "<button type='button' class='btn btn-danger' onclick='newKey()'>New Key</button>";
  html += "<p class='help'>A new key unpairs MELVILLE: pair the box again from MELVILLE afterwards.</p></div>";

  html += "<h2>Key Map</h2><div class='card' style='background:#252525;border-color:#3a3a3a'>";
  html += "<table><tr><th>Key</th><th>Pin</th><th>Sends</th></tr>";
  for (uint8_t k = 0; k < NUM_KEYS; k++) {
    String sends = KEYS[k].addr;
    if (KEYS[k].kind == K_HOLD) sends += " (1 held / 0 released)";
    if (KEYS[k].kind == K_LOC) sends += " &middot; held: /hawthorne/store/" + String(KEYS[k].loc);
    if (KEYS[k].kind == K_IN)  sends = "/hawthorne/goto/in &middot; held: /hawthorne/in";
    if (KEYS[k].kind == K_OUT) sends = "/hawthorne/goto/out &middot; held: /hawthorne/out";
    html += "<tr><td>" + String(KEYS[k].label) + "</td><td>" + pinName(KEYS[k].chip, KEYS[k].pin) + "</td><td>" + sends + "</td></tr>";
  }
  html += "</table><p class='help'>The key layout is set in the firmware (the <code>KEYS</code> table).</p></div>";
  html += "<p class='help'>MCP chips found: 0x20 " + String(mcpOk[0] ? "&#10003;" : "&#10007;") + " &middot; 0x21 " + String(mcpOk[1] ? "&#10003;" : "&#10007;") + " &middot; 0x22 (LOC lamps, optional) " + String(mcpOk[2] ? "&#10003;" : "&#10007;") + "</p>";

  html += "<button type='button' class='btn btn-danger' onclick='resetAll()'>Reset to Defaults</button>";
  html += "<div class='version'>HAWTHORNE TRANSPORT CONTROLLER v3.0 | PEQUOD DESIGN SYSTEM</div>";

  html += "<script>";
  html += "function post(e,url){e.preventDefault();const f=new FormData(e.target);";
  html += "const b=document.getElementById('backlight');if(url=='/lcd')f.set('backlight',b.checked?'true':'false');";
  html += "fetch(url,{method:'POST',body:f}).then(r=>r.text().then(t=>alert(r.ok?'Saved. Restart the device (without GPIO12 held) to use it.':'Error: '+t)));return false}";
  html += "function newKey(){if(confirm('Make a new key? MELVILLE will need to pair with this box again.'))fetch('/newkey',{method:'POST'}).then(()=>location.reload())}";
  html += "function resetAll(){if(confirm('Reset all settings to defaults?'))fetch('/reset',{method:'POST'}).then(()=>location.reload())}";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

void handleNetworkSave() {
  String ip = server.hasArg("mel_ip") ? server.arg("mel_ip") : melvilleIPStr;
  ip.trim();
  IPAddress test;
  if (!parseIPAddress(ip, test)) { server.send(400, "text/plain", "Invalid IP address"); return; }
  long mp = server.hasArg("mel_port") ? server.arg("mel_port").toInt() : melvillePort;
  long dp = server.hasArg("dev_port") ? server.arg("dev_port").toInt() : devicePort;
  if (mp < 1 || mp > 65535 || dp < 1 || dp > 65535) { server.send(400, "text/plain", "Invalid port"); return; }

  melvilleIPStr = ip;
  if (server.hasArg("mel_host")) { melvilleHostname = server.arg("mel_host"); melvilleHostname.trim(); }
  melvillePort = mp;
  devicePort = dp;
  saveNetworkConfig();
  server.send(200, "text/plain", "OK");
}

void handleLCDSave() {
  if (server.hasArg("backlight")) lcdBacklight = server.arg("backlight") == "true";
  if (lcdAvailable) { if (lcdBacklight) lcd.backlight(); else lcd.noBacklight(); }
  saveLCDConfig();
  server.send(200, "text/plain", "OK");
}

void handleNewKey() {
  newDeviceKey();
  ArduinoOTA.setPassword(deviceKey.c_str());
  server.send(200, "text/plain", "OK");
}

void handleReset() {
  preferences.begin(PREFS_NS, false);
  preferences.clear();
  preferences.end();
  loadConfig();
  server.send(200, "text/plain", "OK");
}
