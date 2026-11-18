# HAWTHORNE — the transport controller

Firmware for the HAWTHORNE hardware: a WT32-ETH01 (ESP32 with Ethernet) with two TM1637 displays, a
16x2 LCD and two MCP23017s for 21 keys. It drives Hawthorne through MELVILLE's OSC control surface;
`HAWTHORNE.ino` has the wiring, the key layout and the protocol at the top.

## The first install (USB, once)

Arduino IDE with the **esp32 by Espressif Systems** boards, **3.3.5 or newer** (signed updates need it), board **WT32-ETH01 Ethernet Module**,
partition scheme **Default 4MB with spiffs** (it has the two app slots updates need). Libraries:
Adafruit MCP23017 Arduino Library, TM1637 (Avishay Orpaz), LiquidCrystal_I2C. Open this folder's
`HAWTHORNE.ino` and upload. (`build_opt.h` turns on the signature check; leave it in the folder.)

## Finding MELVILLE (nothing to type in)

Plug the box into the network. It takes its address from the router (DHCP) and calls out to the
whole network until MELVILLE answers; it then remembers where MELVILLE is, and calls out again if
MELVILLE goes quiet. MELVILLE knows the box by its serial (`hawthorne-` and four letters from its
MAC), so a new address from the router changes nothing.

1. MELVILLE → INTERFACE → CONTROL SURFACE: the box shows up as heard from; **ADD HAWTHORNE**.
2. **PAIR**: the box's LCD asks *PAIR MELVILLE?* — press **PLAY** on the box (STOP says no).
   The box makes a new key and gives it to MELVILLE; MELVILLE keeps it with the device.
3. **SETTINGS** (paired boxes): the box's own address (from the router, or fixed) and how it finds
   MELVILLE (by itself, or at a fixed address, for a network a call-out can't cross, such as another
   VLAN), and the LCD backlight. A network change is tried for 30 s: if the box can't reach MELVILLE
   with it, it goes back to what it had.
4. When MELVILLE carries newer firmware than the box runs, the list says so: **UPDATE**. The box
   shows the progress, checks the signature, and restarts into the new firmware. If anything goes
   wrong it keeps running the old one.

## The network reset button

A push button between **GPIO12** and **GND** (no pull-up resistor: GPIO12 must be low as the ESP32
starts, and the button only ever pulls it low). Held **5 seconds**, running or at power-up: the box
goes back to the router's address and finding MELVILLE by itself (its pairing and the rest stay).
Held while it's switched on at all: the Arduino IDE's network upload is on for 10 minutes, with the
box's key as the password (SETTINGS in MELVILLE shows it).

## Releasing new firmware

Change the sketch, bump `firmware_version.h`, push: MELVILLE's build compiles it, signs it with the
`FIRMWARE_SIGNING_KEY` secret, checks the signature against `signing_key.pem`, and ships it in the
installer (`firmware\HAWTHORNE-<version>.bin`). Boxes are offered it when it's newer than theirs.

## The signing key

`signing_key.h` / `signing_key.pem` are the public half: every box carries it and takes only
firmware signed with the matching private key. The private key is the `FIRMWARE_SIGNING_KEY`
repository secret — never commit it. Keep a copy of it somewhere safe: GitHub won't show a secret
again, and boxes out in the world can only be updated by firmware signed with it (without it, each
would need a USB install of firmware with a new public key).
