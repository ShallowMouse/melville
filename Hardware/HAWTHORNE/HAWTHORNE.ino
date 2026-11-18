/* HAWTHORNE - transport controller for Hawthorne (in MELVILLE)  (version: firmware_version.h)
   Hardware is named after the software it controls.
   (converted from the ESP32 DAW Remote v2.2 for REAPER)

   Talks to MELVILLE's OSC control surface ("HAWTHORNE" section of OscControl.h):
     - sends keys to MELVILLE on UDP 8100
     - MELVILLE answers with every lamp and readout on UDP 9100 (to whichever address it heard from)
     - sends /melville/sync every 3 s so MELVILLE keeps answering (and to spot a lost link)
   In MELVILLE: INTERFACE window -> OSC on, listen 8100, reply 9100 (the defaults).

   FINDING EACH OTHER (nothing to type in)
     - the box takes its address from the router (DHCP), unless MELVILLE's SETTINGS give it a fixed one
     - until it hears MELVILLE it calls out to the whole network (a broadcast hello + sync every 3 s,
       and to the last MELVILLE it knew); MELVILLE sees it by its name and serial ("hawthorne-3f2a")
     - the first answer from MELVILLE tells the box where MELVILLE is; it remembers that, and calls
       out again if MELVILLE goes quiet. MELVILLE knows the box by its serial, so a new address from
       the router doesn't matter to either side
     - MELVILLE's SETTINGS (INTERFACE -> DEVICES, once the box is paired) can fix the box's address
       and MELVILLE's (for networks a broadcast can't cross). A change is tried for 30 s: if the box
       can't reach MELVILLE with it, it goes back to what it had.
     - NETWORK RESET: a push button from GPIO12 to GND (no pull-up resistor: GPIO12 must be low as the
       ESP32 starts). Held 5 s, running or at power-up: back to DHCP and finding MELVILLE by itself.
       Held at power-up at all: the Arduino IDE's network upload is on for 10 minutes, with the box's
       key as its password (MELVILLE's SETTINGS window shows it).

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
     - the Arduino IDE's network upload is only on for 10 minutes after a power-up with GPIO12 held
       (see NETWORK RESET), with this box's key as the password.
     - settings: /melville/settings/get -> /melville/settings/now (what it has); /melville/settings
       key, ip mode, ip, mask, gateway, dns, melville mode, melville ip, backlight -> /melville/settings/done
       serial "kept" | "trying" (or /melville/settings/refused serial why); after a try, once MELVILLE
       is heard again: /melville/settings/result serial "kept" | "reverted"

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

   NETWORK RESET: a push button from GPIO12 to GND, held 5 s (see FINDING EACH OTHER)

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
// (signed updates: the "esp32 by Espressif Systems" boards 3.3.5 or newer, and build_opt.h in this folder)
#if !__has_include(<Updater_Signing.h>)
#error "Update the boards: Tools > Board > Boards Manager > esp32 by Espressif Systems > 3.3.5 or newer (signed updates need it)"
#endif
#ifndef UPDATE_SIGN
#error "build_opt.h is missing from the sketch folder (it turns on the signature check): keep all of HAWTHORNE's files together"
#endif
#include "firmware_version.h"
#include "signing_key.h"

// ---------- DEFAULT CONFIG ----------
const char* DEVICE_NAME = "HAWTHORNE";              // what it is (named after the software it controls)
String deviceHostname = "hawthorne";                // this unit on the network: "hawthorne-" + the last four
                                                    // hex digits of its MAC, so several can share a network
const unsigned int DEFAULT_MELVILLE_PORT = 8100;    // MELVILLE listens here
const unsigned int DEFAULT_DEVICE_PORT = 9100;      // MELVILLE answers here
String deviceKey;                                   // this box's key: MELVILLE needs it to update it (made on first start,
                                                    // and again each time it's paired)
const char* PREFS_NS = "hawthorne-tx";

unsigned int melvillePort = DEFAULT_MELVILLE_PORT;
unsigned int devicePort = DEFAULT_DEVICE_PORT;
bool lcdBacklight = true;

// The network: the box's own address (from the router, or fixed) and how it finds MELVILLE.
struct NetConfig {
  bool fixedIp = false;               // false: DHCP
  String ip, mask, gateway, dns;      // (fixedIp)
  bool fixedMelville = false;         // false: found by itself
  String melvilleIp;                  // (fixedMelville)
};
NetConfig net;
String melvilleLast;                  // the last MELVILLE the box heard from (where it calls first)
bool trial = false;                   // trying new settings: kept if MELVILLE is heard within TRIAL_MS
bool reverted = false;                // a try failed: tell MELVILLE when it's heard again
bool trialKept = false;
const unsigned long TRIAL_MS = 30000;
unsigned long trialFrom = 0;

IPAddress melvilleIP;                 // where MELVILLE is (0.0.0.0: not known yet)

// ---------- PINS ----------
const uint8_t CLK1_PIN = 32, DIO1_PIN = 33;         // 6-digit
const uint8_t CLK2_PIN = 2,  DIO2_PIN = 4;          // 4-digit
const uint8_t I2C_SDA = 14,  I2C_SCL = 15;
const uint8_t RESET_PIN = 12;                       // NETWORK RESET: a button to GND
const unsigned long RESET_HOLD_MS = 5000;
const unsigned long IDE_UPLOAD_MS = 10UL * 60UL * 1000UL;
unsigned long resetDownMs = 0;
unsigned long ideUploadUntil = 0;                    // the Arduino IDE's network upload is on until then

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

// Where to send: MELVILLE, once it's known; everyone on the network until then.
IPAddress melvilleTarget() {
  if (melvilleIP != IPAddress((uint32_t)0)) return melvilleIP;
  return ETH.broadcastIP();
}

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
void newDeviceKey();
void sendHello();
void sendOSCStrings(const char* address, std::initializer_list<const char*> args);
void sendOSCStringsTo(IPAddress to, const char* address, std::initializer_list<const char*> args);
void setupUpdateServer();
void heardFrom(IPAddress from);
void handleSettings(const String* args, int n);
void sendSettingsNow();
void networkReset();
void checkResetButton();
void revertTrial();
bool fromMelville(IPAddress from);
bool parseIPAddress(const String& s, IPAddress& ip);

// ---------- CONFIG ----------
static void loadNet(const char* prefix, NetConfig& n) {
  auto k = [prefix](const char* name) { return String(prefix) + name; };
  n.fixedIp       = preferences.getBool(k("fixip").c_str(), false);
  n.ip            = preferences.getString(k("ip").c_str(), "");
  n.mask          = preferences.getString(k("mask").c_str(), "255.255.255.0");
  n.gateway       = preferences.getString(k("gw").c_str(), "");
  n.dns           = preferences.getString(k("dns").c_str(), "");
  n.fixedMelville = preferences.getBool(k("fixmel").c_str(), false);
  n.melvilleIp    = preferences.getString(k("melip").c_str(), "");
}

static void saveNet(const char* prefix, const NetConfig& n) {
  auto k = [prefix](const char* name) { return String(prefix) + name; };
  preferences.putBool(k("fixip").c_str(), n.fixedIp);
  preferences.putString(k("ip").c_str(), n.ip);
  preferences.putString(k("mask").c_str(), n.mask);
  preferences.putString(k("gw").c_str(), n.gateway);
  preferences.putString(k("dns").c_str(), n.dns);
  preferences.putBool(k("fixmel").c_str(), n.fixedMelville);
  preferences.putString(k("melip").c_str(), n.melvilleIp);
}

void loadConfig() {
  preferences.begin(PREFS_NS, true);
  loadNet("n_", net);
  melvilleLast     = preferences.getString("mel_last", preferences.getString("mel_ip", ""));   // (3.1: the address typed in)
  melvillePort     = preferences.getUInt("mel_port", DEFAULT_MELVILLE_PORT);
  devicePort       = preferences.getUInt("dev_port", DEFAULT_DEVICE_PORT);
  lcdBacklight     = preferences.getBool("backlight", true);
  deviceKey        = preferences.getString("dev_key", "");
  trial            = preferences.getBool("trial", false);
  reverted         = preferences.getBool("reverted", false);
  preferences.end();
  if (deviceKey.length() != 32) newDeviceKey();      // (first start)
  IPAddress last;
  if (net.fixedMelville && parseIPAddress(net.melvilleIp, last)) melvilleIP = last;
  else if (parseIPAddress(melvilleLast, last)) melvilleIP = last;
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

// (a mask has its ones first: 255.255.255.0 yes, 255.0.255.0 no)
static bool validMask(const IPAddress& m) {
  const uint32_t v = (uint32_t(m[0]) << 24) | (uint32_t(m[1]) << 16) | (uint32_t(m[2]) << 8) | uint32_t(m[3]);
  return v != 0 && ((~v + 1) & ~v) == 0;
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
    case ARDUINO_EVENT_ETH_CONNECTED:
      if (net.fixedIp) eth_connected = true;       // (a fixed address: up as soon as the cable is)
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
  Udp.beginPacket(melvilleTarget(), melvillePort);
  Udp.write(buf, i);
  Udp.endPacket();
}

// An OSC message of strings (hello, paired, settings), to MELVILLE (or everyone, until it's found).
void sendOSCStrings(const char* address, std::initializer_list<const char*> args) {
  sendOSCStringsTo(melvilleTarget(), address, args);
}

void sendOSCStringsTo(IPAddress to, const char* address, std::initializer_list<const char*> args) {
  if (!eth_connected) return;
  uint8_t buf[512];
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
  char tags[24] = ",";
  int n = 1;
  for (size_t k = 0; k < args.size() && n < 23; k++) tags[n++] = 's';
  tags[n] = 0;
  if (!put(address) || !put(tags)) return;
  for (const char* a : args) if (!put(a)) return;
  Udp.beginPacket(to, melvillePort);
  Udp.write(buf, i);
  Udp.endPacket();
}

// Who this is: MELVILLE's DEVICES list shows it, with its firmware.
void sendHello() {
  sendOSCStrings("/melville/hello", { "hawthorne", DEVICE_NAME, deviceHostname.c_str(), FIRMWARE_VERSION });
}

// Not hearing MELVILLE (and not told where it is): call out to the whole network, and to the last
// MELVILLE it knew (which a broadcast may not reach). Whichever answers is MELVILLE.
void callOut() {
  sendOSCStringsTo(ETH.broadcastIP(), "/melville/hello", { "hawthorne", DEVICE_NAME, deviceHostname.c_str(), FIRMWARE_VERSION });
  sendOSCStringsTo(ETH.broadcastIP(), "/melville/sync", {});
  IPAddress last;
  if (parseIPAddress(melvilleLast, last) && last != ETH.broadcastIP()) {
    sendOSCStringsTo(last, "/melville/hello", { "hawthorne", DEVICE_NAME, deviceHostname.c_str(), FIRMWARE_VERSION });
    sendOSCStringsTo(last, "/melville/sync", {});
  }
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

  if (i >= len || p[i] != ',') {
    if (strcmp(addr, "/melville/settings/get") == 0) { lastHeardMs = millis() ? millis() : 1; sendSettingsNow(); return; }
    handleOSC(addr, 0, 1.0f, nullptr);
    return;
  }
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
    if (strncmp(addr, "/melville/settings", 18) == 0) {   // (several strings)
      String args[12];
      int n = 0, at = i;
      const char* tags = (const char*)(p + tagsAt + 1);
      while (n < 12 && tags[n] == 's') {
        const int next = oscString(p, len, at);
        if (next < 0) break;
        args[n++] = String((const char*)(p + at));
        at = next;
      }
      lastHeardMs = millis() ? millis() : 1;
      if (strcmp(addr, "/melville/settings") == 0) handleSettings(args, n);
      return;
    }
    handleOSC(addr, 's', 0.0f, (const char*)(p + i));
  } else if (strcmp(addr, "/melville/settings/get") == 0) {
    lastHeardMs = millis() ? millis() : 1;
    sendSettingsNow();
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
// Back to DHCP and finding MELVILLE by itself (the key, the backlight and the rest stay).
void networkReset() {
  preferences.begin(PREFS_NS, false);
  NetConfig fresh;
  saveNet("n_", fresh);
  saveNet("b_", fresh);
  preferences.putString("mel_last", "");
  preferences.remove("mel_ip");
  preferences.putBool("trial", false);
  preferences.putBool("reverted", false);
  preferences.end();
}

// GPIO12 held: a countdown on the LCD, and after RESET_HOLD_MS the network goes back to its start.
// Returns true if it reset.
bool resetCountdown() {
  if (digitalRead(RESET_PIN)) { resetDownMs = 0; return false; }
  if (resetDownMs == 0) resetDownMs = millis() ? millis() : 1;
  const unsigned long held = millis() - resetDownMs;
  if (held >= RESET_HOLD_MS) {
    networkReset();
    lcdTwoLines("NETWORK RESET", "DHCP, FIND AUTO");
    delay(1500);
    return true;
  }
  if (held > 300) {
    const int left = (int)((RESET_HOLD_MS - held + 999) / 1000);
    lcdTwoLines("NETWORK RESET?", "HOLD " + String(left) + "s...");
  }
  return false;
}

// Running: held 5 s, reset and start again; let go sooner, nothing.
void checkResetButton() {
  const bool wasDown = resetDownMs != 0;
  if (resetCountdown()) ESP.restart();
  if (wasDown && resetDownMs == 0) { lcd.clear(); memset(lastLcd, 0xFF, sizeof(lastLcd)); lcdDirty = true; }
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

  loadConfig();

  // GPIO12 held at power-up: the Arduino IDE's network upload for a while; held 5 s, a network reset too
  pinMode(RESET_PIN, INPUT_PULLUP);
  delay(10);
  if (!digitalRead(RESET_PIN)) {
    ideUploadUntil = IDE_UPLOAD_MS;
    lcdTwoLines("IDE UPLOAD ON", "HOLD 5s: RESET");
    const unsigned long t0 = millis();
    while (!digitalRead(RESET_PIN) && millis() - t0 < RESET_HOLD_MS + 500) {
      if (resetCountdown()) { loadConfig(); break; }
      delay(50);
    }
    resetDownMs = 0;
  }

  setupMCPs();
  if (!mcpOk[0] || !mcpOk[1]) {
    lcdTwoLines("MCP ERROR!", String(mcpOk[0] ? "" : "0x20 ") + (mcpOk[1] ? "" : "0x21"));
    while (1) { if (resetCountdown()) ESP.restart(); delay(100); }
  }

  Network.onEvent(onEthEvent);
  ETH.begin(ETH_PHY_LAN8720, 1, 23, 18, 16, ETH_CLOCK_GPIO0_IN);
  ETH.setHostname(deviceHostname.c_str());
  if (net.fixedIp) {
    IPAddress ip, mask, gw, dns;
    if (parseIPAddress(net.ip, ip) && parseIPAddress(net.mask, mask)) {
      if (!parseIPAddress(net.gateway, gw)) gw = IPAddress((uint32_t)0);
      if (!parseIPAddress(net.dns, dns)) dns = gw;
      ETH.config(ip, gw, mask, dns);
    }
  }

  lcdTwoLines(DEVICE_NAME, trial ? "TRYING NEW SETUP" : "Wait Ethernet...");
  while (!eth_connected) {
    if (resetCountdown()) ESP.restart();
    // (trying new settings and the network never comes up: go back)
    if (trial && millis() > TRIAL_MS + 15000) revertTrial();
    delay(100);
  }
  trialFrom = millis();

  if (MDNS.begin(deviceHostname.c_str())) MDNS.addService("osc", "udp", devicePort);

  lcdTwoLines(deviceHostname, ETH.localIP().toString());
  delay(1500);
  lcdTwoLines("> MELVILLE", net.fixedMelville ? net.melvilleIp : String("FINDING..."));
  delay(800);

  if (!lcdBacklight) lcd.noBacklight();

  Udp.begin(devicePort);
  delay(100);
  setupUpdateServer();                             // (MELVILLE's firmware updates)
  if (ideUploadUntil) {
    // the Arduino IDE's network upload: only after a power-up with GPIO12 held (someone at the box), with its key
    ArduinoOTA.setHostname(deviceHostname.c_str());
    ArduinoOTA.setPassword(deviceKey.c_str());
    ArduinoOTA.begin();
    ideUploadUntil = millis() + IDE_UPLOAD_MS;
  }

  lcd.clear();
  memset(lastLcd, 0xFF, sizeof(lastLcd));
  if (net.fixedMelville) { sendHello(); sendOSC("/melville/sync", 1.0f); }
  else callOut();
  lastSyncMs = millis();
}

// ---------- LOOP ----------
void loop() {
  server.handleClient();                          // (a firmware update from MELVILLE)
  if (ideUploadUntil) {
    if ((long)(millis() - ideUploadUntil) < 0) ArduinoOTA.handle();
    else { ArduinoOTA.end(); ideUploadUntil = 0; }
  }
  checkResetButton();
  if (resetDownMs) return;                        // (the countdown has the LCD)

  if (pairAsking && millis() - pairAskedMs > PAIR_WAIT_MS) {   // (no answer: no)
    pairAsking = false;
    sendOSCStrings("/melville/pair/declined", { deviceHostname.c_str() });
    flash("NOT PAIRED");
  }
  // trying new settings: MELVILLE heard - keep them; not heard in time - go back
  if (trial && !trialKept && millis() - trialFrom > TRIAL_MS) revertTrial();

  for (int n = 0; n < 16; n++) {                  // (a sync answer is ~40 small packets)
    int size = Udp.parsePacket();
    if (size <= 0) break;
    if (size > (int)sizeof(udpBuf)) size = sizeof(udpBuf);
    const IPAddress from = Udp.remoteIP();
    int got = Udp.read(udpBuf, size);
    if (got <= 0) continue;
    if (!fromMelville(from)) continue;            // (another MELVILLE, or something else: not ours)
    const unsigned long before = lastHeardMs;
    parseOSCPacket(udpBuf, got, 0);
    if (lastHeardMs != before) heardFrom(from);
  }

  scanKeys();

  const unsigned long now = millis();
  if (now - lastSyncMs >= SYNC_INTERVAL_MS) {
    if (melvilleOnline() || net.fixedMelville) {
      sendHello();
      sendOSC("/melville/sync", 1.0f);
    } else {
      callOut();                                  // (MELVILLE gone quiet, or never found: ask everyone)
    }
    lastSyncMs = now;
  }

  updateLamps();
  updateDisplays();
}

// Is this packet from MELVILLE? Told where MELVILLE is: only from there. Finding it by itself: from
// the one it's talking to, or from anyone while it isn't hearing MELVILLE (that one is MELVILLE now).
bool fromMelville(IPAddress from) {
  if (net.fixedMelville) {
    IPAddress fixed;
    return parseIPAddress(net.melvilleIp, fixed) && from == fixed;
  }
  return !melvilleOnline() || from == melvilleIP;
}

// MELVILLE answered from here: remember it (in flash only when it moves), and finish a try.
void heardFrom(IPAddress from) {
  if (!net.fixedMelville && from != melvilleIP) {
    melvilleIP = from;
    const String a = from.toString();
    if (a != melvilleLast) {
      melvilleLast = a;
      preferences.begin(PREFS_NS, false);
      preferences.putString("mel_last", melvilleLast);
      preferences.end();
    }
  }
  if (trial && !trialKept) {                      // the new settings reach MELVILLE: keep them
    trialKept = true;
    trial = false;
    preferences.begin(PREFS_NS, false);
    preferences.putBool("trial", false);
    preferences.end();
    sendOSCStrings("/melville/settings/result", { deviceHostname.c_str(), "kept" });
    flash("NEW SETUP KEPT");
  }
  if (reverted) {
    reverted = false;
    preferences.begin(PREFS_NS, false);
    preferences.putBool("reverted", false);
    preferences.end();
    sendOSCStrings("/melville/settings/result", { deviceHostname.c_str(), "reverted" });
    flash("WENT BACK");
  }
}

// The new settings never reached MELVILLE: put the old ones back and start again with them.
void revertTrial() {
  preferences.begin(PREFS_NS, false);
  NetConfig old;
  loadNet("b_", old);
  saveNet("n_", old);
  preferences.putBool("trial", false);
  preferences.putBool("reverted", true);
  preferences.end();
  lcdTwoLines("NO MELVILLE", "GOING BACK...");
  delay(1500);
  ESP.restart();
}

// ---------- SETTINGS (from MELVILLE's SETTINGS window) ----------
static const char* yesNo(bool b) { return b ? "fixed" : "auto"; }

// What the box has: serial, its address now (and how it got it), the fixed address it would use,
// how it finds MELVILLE, the backlight and the firmware.
void sendSettingsNow() {
  const String ip = ETH.localIP().toString(), mask = ETH.subnetMask().toString(), gw = ETH.gatewayIP().toString(),
               dns = ETH.dnsIP().toString(), mel = melvilleIP.toString();
  sendOSCStrings("/melville/settings/now", { deviceHostname.c_str(),
                                             net.fixedIp ? "fixed" : "dhcp", ip.c_str(), mask.c_str(), gw.c_str(), dns.c_str(),
                                             net.ip.c_str(), net.mask.c_str(), net.gateway.c_str(), net.dns.c_str(),
                                             yesNo(net.fixedMelville), net.melvilleIp.c_str(), mel.c_str(),
                                             lcdBacklight ? "on" : "off", FIRMWARE_VERSION });
}

void refuse(const char* why) {
  sendOSCStrings("/melville/settings/refused", { deviceHostname.c_str(), why });
}

// key, "dhcp" | "fixed", ip, mask, gateway, dns, "auto" | "fixed", melville ip, "on" | "off"
void handleSettings(const String* a, int n) {
  if (n < 9) { refuse("MELVILLE sent too little"); return; }
  if (!sameKey(a[0], deviceKey)) { refuse("not paired with this MELVILLE: pair it again"); return; }
  NetConfig want;
  want.fixedIp = a[1] == "fixed";
  want.ip = a[2]; want.mask = a[3]; want.gateway = a[4]; want.dns = a[5];
  want.fixedMelville = a[6] == "fixed";
  want.melvilleIp = a[7];
  IPAddress t;
  if (want.fixedIp) {
    if (!parseIPAddress(want.ip, t)) { refuse("the box's address isn't an address"); return; }
    if (!parseIPAddress(want.mask, t) || !validMask(t)) { refuse("the subnet mask isn't one"); return; }
    if (want.gateway.length() && !parseIPAddress(want.gateway, t)) { refuse("the gateway isn't an address"); return; }
    if (want.dns.length() && !parseIPAddress(want.dns, t)) { refuse("the DNS server isn't an address"); return; }
  }
  if (want.fixedMelville && !parseIPAddress(want.melvilleIp, t)) { refuse("MELVILLE's address isn't an address"); return; }

  // the backlight: straight away
  const bool light = a[8] != "off";
  if (light != lcdBacklight) {
    lcdBacklight = light;
    if (lcdAvailable) { if (lcdBacklight) lcd.backlight(); else lcd.noBacklight(); }
    saveLCDConfig();
  }

  const bool same = want.fixedIp == net.fixedIp && want.fixedMelville == net.fixedMelville
                 && (!want.fixedIp || (want.ip == net.ip && want.mask == net.mask && want.gateway == net.gateway && want.dns == net.dns))
                 && (!want.fixedMelville || want.melvilleIp == net.melvilleIp);
  if (same) { sendOSCStrings("/melville/settings/done", { deviceHostname.c_str(), "kept" }); return; }

  // the network: tried on a restart, kept only if MELVILLE is heard with it
  preferences.begin(PREFS_NS, false);
  saveNet("b_", net);
  saveNet("n_", want);
  preferences.putBool("trial", true);
  preferences.putBool("reverted", false);
  preferences.end();
  sendOSCStrings("/melville/settings/done", { deviceHostname.c_str(), "trying" });
  lcdTwoLines("NEW NETWORK", "RESTARTING...");
  delay(600);
  ESP.restart();
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

static String pinName(uint8_t chip, uint8_t pin) {
  return String("MCP") + String(chip + 1) + " " + (pin < 8 ? "GPA" : "GPB") + String(pin % 8);
}
