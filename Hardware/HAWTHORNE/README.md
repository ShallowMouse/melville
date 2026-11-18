# HAWTHORNE — the transport controller

Firmware for the HAWTHORNE hardware: a WT32-ETH01 (ESP32 with Ethernet) with two TM1637 displays, a
16x2 LCD and two MCP23017s for 21 keys. It drives Hawthorne through MELVILLE's OSC control surface;
`HAWTHORNE.ino` has the wiring, the key layout and the protocol at the top.

## The first install (USB, once)

Arduino IDE with the **esp32 by Espressif** boards (3.3.x), board **WT32-ETH01 Ethernet Module**,
partition scheme **Default 4MB with spiffs** (it has the two app slots updates need). Libraries:
Adafruit MCP23017 Arduino Library, TM1637 (Avishay Orpaz), LiquidCrystal_I2C. Open this folder's
`HAWTHORNE.ino` and upload. (`build_opt.h` turns on the signature check; leave it in the folder.)

## Pairing and updates (from MELVILLE, after that)

1. MELVILLE → INTERFACE → DEVICES: the box shows up as heard from; **ADD HAWTHORNE**.
2. **PAIR**: the box's LCD asks *PAIR MELVILLE?* — press **PLAY** on the box (STOP says no).
   The box makes a new key and gives it to MELVILLE; MELVILLE keeps it with the device.
3. When MELVILLE carries newer firmware than the box runs, the list says so: **UPDATE**. The box
   shows the progress, checks the signature, and restarts into the new firmware. If anything goes
   wrong it keeps running the old one.

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

The settings page (hold GPIO12 to GND while it starts) shows the box's own key, which is also the
password for the Arduino IDE's network upload — on only in that mode.
