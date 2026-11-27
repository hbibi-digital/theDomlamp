# Firmware of theDomlamp

*English · [Nederlands](README.nl.md)*

The lamp runs plain WLED 16.0.1 with one usermod of its own: `domlamp`.
The usermod fetches the light plan every minute (`https://utrechtdom.city/api/licht`) and sets the seven zones of the lamp.
Everything else is WLED itself: setting up wifi, the web page, updates, the current limit.

The firmware runs on two ESP32-C3 boards (since 25 November 2026). What has and has not been tested is at the bottom.

## What is in this directory

| Path | What |
| --- | --- |
| `usermod_domlamp/usermod_domlamp.cpp` | The WLED side: segments, settings, info page. |
| `usermod_domlamp/domlamp_core.*` | The decisions: reading the plan, choosing the scene, candle, fallback. Plain C++, without Arduino. |
| `usermod_domlamp/domlamp_fetch.*` | Fetching over HTTPS, in a task of its own. |
| `usermod_domlamp/domlamp_link.*` | Linking via the server: the open connection to the relay service, in a task of its own. |
| `usermod_domlamp/domlamp_net.h` | The TLS library and its settings, shared by fetching and linking. |
| `usermod_domlamp/domlamp_certs.h` | The two root certificates of Let's Encrypt. |
| `usermod_domlamp/library.json` | Turns the directory into a WLED usermod (this is how WLED 16 wants it). |
| `platformio_override.ini` | The build environments `domlamp_esp32` and `domlamp_esp32c3`. |
| `build.sh` | Fetches WLED (outside this repo) and builds. |
| `test/` | Test of the core on the Mac, without hardware. |

## Building

Required: git, node/npm and PlatformIO (`uv tool install platformio`).

```sh
./build.sh                    # ESP32, 4 MB
./build.sh domlamp_esp32c3    # ESP32-C3 (Seeed Studio XIAO ESP32-C3), data on GPIO 10 (pin D10)
```

The script clones WLED `v16.0.1` to `~/.cache/domlamp-fw/WLED` (or to `$DOMLAMP_WLED_DIR`).
It creates a symlink `usermods/domlamp` to this directory and copies `platformio_override.ini`.
The firmware ends up in `build/<omgeving>.bin`. That directory is in `.gitignore`.

## Flashing

### Without building it yourself

Every release of theDomlamp carries two ready-made files for the Seeed Studio XIAO ESP32-C3 (`release.sh`
makes them):

- `theDomlamp-<version>-xiao-esp32c3-factory.bin`: bootloader, partition table and firmware in one file, for
  the first time. Flash it at address `0x0`:
  - in the browser (Chrome or Edge), with the board on USB: <https://espressif.github.io/esptool-js/>,
    connect, address `0x0`, choose the file, *Program*;
  - or with esptool: `esptool.py --chip esp32c3 write_flash 0x0 theDomlamp-<version>-xiao-esp32c3-factory.bin`.

  If the board does not show up, hold the BOOT button while you plug in the USB cable.
- `theDomlamp-<version>-xiao-esp32c3-update.bin`: the firmware alone, for a lamp that already runs. In WLED go
  to *Config → Security & Updates → Manual OTA Update* and choose the file; the settings stay.

Check a downloaded file with `shasum -a 256` against `SHA256SUMS.txt` from the same release. The firmware's
version is on the lamp's info page under "Dom firmware".

### From source

- **First time, via USB:** `./build.sh domlamp_esp32 -t upload --upload-port /dev/cu.usbserial-XXXX`.
  That also writes the bootloader and the partition table.
- **After that, or on a board that already has WLED on it:** in WLED go to *Config → Security & Updates → Manual OTA Update* and choose `build/domlamp_esp32.bin`.
  The settings are then kept.
- The Dig2Go already has WLED. Whether an update from that version to 16.0.1 goes well without erasing has not been tried.

## First settings in WLED

1. Wifi: the lamp starts as an access point named `theDomlamp-` plus six characters (password `wled1234`, WLED's default). Choose your network there.
2. *Config → LED Preferences*: type SK6812 RGBW, the number of pixels, the data pin.
   The build sets as a start: SK6812 RGBW, 52 pixels, and a current limit of 2000 mA on the C3 (where all the current runs through the board itself) and 2500 mA on the ESP32 build. By calculation 52 pixels draw about 1.0 A in warm white and 1.9 A in orange (20 mA a channel); this has not been measured.
   Data pin: GPIO 16 on the ESP32 build (the WLED default), GPIO 10 on the C3 build.
   Set *Auto-calculate W channel from RGB* (and *Auto-calculate white* at the strip) to **None**. The plan drives the white itself.
3. *Config → Time & Macros*: NTP is already on in this build, time zone Central Europe. Check that the clock is right.
4. Leave the transition style of WLED (blending) on *Fade*. With another style the candle looks different.

## Segments: which pixel belongs to which zone

The strip runs up on one side and down on the other side. Each height therefore has two pixels.
That is why each zone gets **two segments** by default:

| Zone | Segment up | Segment down |
| --- | --- | --- |
| voet | 0 | 13 |
| eerste_vierkant | 1 | 12 |
| omloop1 | 2 | 11 |
| tweede_vierkant | 3 | 10 |
| omloop2 | 4 | 9 |
| lantaarn | 5 | 8 |
| dak | 6 | 7 |

- The usermod sets only the **first colour** and the **brightness** (opacity) of those segments.
  Which pixels are in a segment is up to you in WLED.
- If WLED has only one segment so far, the usermod creates fourteen at startup, evenly divided. That is only a start.
- After that, count on the real strip and set *Start* and *Stop* per segment.
  A pixel at the height of a partition is left outside every segment. It stays off.
- **Save the segments as a preset and choose that preset at *Apply preset at boot*.**
  WLED remembers segments only in presets. Without that they are gone after a restart.
- If a segment does not exist, the usermod skips it. Nothing breaks.
- Prefer seven segments? Turn *two-segments-per-zone* off. Then zone 1–7 belongs to segment 0–6.
  You then bring the two sides together with a `ledmap.json` of WLED.

The up/down order is an assumption. If your strip runs differently, swap the ranges of the segments.

## Settings of the usermod

*Config → Usermods → Domlamp.* (in the settings and in the JSON API the usermod is still called `Domlamp`; renaming would break existing lamps and presets)

| Field | Default | Meaning |
| --- | --- | --- |
| `enabled` | on | Follow the Dom Tower. |
| `url` | `https://utrechtdom.city/api/licht` | Where the plan is. `http://` is also allowed (for a trial at home). `https` only on port 443. |
| `interval` | 0 | Seconds between two fetches. 0 = what the server asks for (`volgende_s`, currently 60). |
| `verify-certificate` | on | Verify the certificate of the server. |
| `first-segment` | 0 | The segment of the base. |
| `two-segments-per-zone` | on | See above. |
| `create-segments` | on | Create segments if WLED has only one. |
| `fallback-on` / `fallback-off` | 17:00 / 23:00 | Warm white without a plan, local time. May cross midnight. Equal times = always on. |
| `fallback-level` | 255 | Brightness of that warm white. |
| `store-plan-hours` | 6 | How often at most the plan goes to flash. 0 = never. |

## Changing the lamp yourself

- **The master brightness and the on/off button of WLED are yours.** The usermod does not touch them. Dimming is therefore always possible.
- **The usermod writes only when the scene changes.** Not every minute.
  If you change a colour by hand, it stays until the next scene change of the tower. There are a few of those per day.
- **If you want it to stay, turn following off.** That can be done in three ways:
  - the button at *Dom tower* on the info page;
  - the checkbox `enabled` in the settings (that is also the state after a restart);
  - in a preset or via the JSON API: `{"Domlamp":{"enabled":false}}`, and `true` to follow again.
- If you turn following back on, the lamp immediately shows the scene of that moment.
- A preset that you save also remembers whether following was on.

## Linking via the server (trial)

Built and measured on the second lamp on 26 November 2026; the service runs live (see "Not tested" for what is still open).

With the setting `link-url` (empty = off; intended: `https://utrechtdom.city/api/koppel`) the lamp keeps one
connection open to the relay service (`web/deploy/koppel/koppel.py`). A browser that knows the code of the lamp
puts its zones there; the lamp shows them with a transition of 120 ms and leaves its segments alone for 12 s
after the last frame, just as with the direct link. After that it follows the Dom Tower again.

- The code (`link-code`, eight characters) is created by the lamp itself on first use and is shown on the info page
  at "Dom link code", with the state of the connection and the number of frames.
- The connection runs in a task of its own (`domlamp_link.cpp`), with the same TLS library and
  certificate verification as fetching the plan (`domlamp_net.h`). Over https it holds an estimated
  45 kB of memory for as long as it is open; it does not start below 70 kB free.
- If the connection drops, the lamp tries again after 5 s, increasing to 60 s.
- The lamp requests the stream with HTTP/1.0, so that the response does not arrive in pieces (chunks).

## Transitions

- `vervagen`: all zones at once, in `duur_s` seconds.
- `kaars_aan`: zone after zone, from the roof to the base. Each zone gets a seventh of the time.
- `kaars_uit`: zone after zone, from the base to the roof.
- The transition from the plan applies only if the lamp experiences the change within 90 seconds.
  If the lamp starts up later, it fades to the right scene in 2 seconds.

## Without internet

- The lamp follows the last plan on its own clock. A plan runs 36 hours ahead.
- The plan goes to flash at most once per 6 hours (about 0.6 kB, four times a day). That does not noticeably wear the memory.
  After a restart the lamp uses that plan, **if it knows the time**. Without internet and without a clock that is not possible.
- If the plan has run out, or there never was one: warm white (only the W channel) between `fallback-on` and `fallback-off`.
- If the lamp does not know the time, it is warm white **on**. A lamp that stays dark looks broken.
- After a failed attempt the lamp waits twice as long each time, up to at most 15 minutes.

## The clock

- The lamp uses the clock of WLED (NTP).
- With each plan it compares that with `tijd` from the server. A difference of more than 2 minutes is shown on the info page (*Dom clock*).
  The lamp then keeps following its own clock. A difference can also mean that the server no longer refreshes the plan.
- If WLED has no clock, the lamp takes over `tijd` from the server. That is only possible if `verify-certificate` is off or the URL is `http://`: verifying the certificate requires the date.

## Certificate

WLED 16 is built on a framework in which TLS is turned off. That is why it comes with a TLS library of its own: BearSSL, via `ESP_SSLClient` 3.1.3.

- **Choice: the certificate is verified**, against the two roots of Let's Encrypt (ISRG Root X1 and X2). They are in the firmware (2.7 kB).
- **The risk:** if the server switches to another issuer, the lamp refuses the plan. Caddy can do that by itself (fallback to ZeroSSL).
  You then see an error on the info page and the lamp falls back to the old plan and after that to warm white.
  Solution: turn `verify-certificate` off, or new firmware with the right root.
- If verification is off, the connection is still encrypted but someone on the network can impersonate the server.
- The roots expire in 2035 and 2040.
- Cost, measured in the build: see *Sizes*. During fetching it costs about 45 kB of working memory, a few seconds per minute. The latter is estimated, not measured.

## Info page

Under *Info* in WLED:

- *Dom tower*: the button for following on/off.
- *Dom scene*: the current scene, and where it comes from (plan, stored plan, fallback).
- *Dom next*: the next change.
- *Dom last fetch*: how long ago, and how long it took.
- *Dom error*: the last error, if there is one.
- *Dom clock*: only if the clock is off by more than 2 minutes.
- *Dom fetch stack*: how much stack the fetch task had left. Useful for the first trial on hardware.

## Sizes (from the build, WLED 16.0.1, 4 MB flash, app partition 1 572 864 bytes)

| Build | Flash used | Free | RAM (static) |
| --- | --- | --- | --- |
| Bare WLED, ESP32 (`esp32dev`) | 1 297 849 (82.5%) | 275 015 | 81 552 |
| theDomlamp, ESP32 | 1 407 089 (89.5%) | 165 775 | 85 632 |
| Bare WLED, ESP32-C3 (`esp32c3dev`) | 1 264 840 (80.4%) | 308 024 | 71 768 |
| theDomlamp, ESP32-C3 | 1 375 494 (87.5%) | 197 370 | 75 848 |

The usermod with TLS therefore costs about 110 kB of flash and 4 kB of fixed RAM.
"RAM" here is what the linker allocates, not the free memory on a running lamp. That has not been measured.

## Tested

**On the Mac (`test/run.sh`, 530 checks, with AddressSanitizer and UBSan):**

- reading the plan, including the real example and a real response from the server (chunked);
- truncated and wrong responses: refused, no crash;
- which scene applies when, and when the plan has run out;
- the candle, compared with `candle()` from the web app;
- the fallback times, including across midnight;
- waiting times after errors, reading URLs;
- packing and unpacking the plan for flash, with damaged bytes.

**Compiled:** both environments build without errors and without warnings in the project's own code.

**Checked separately on the Mac:** BearSSL (the regular version, not the copy in the library) connects to the real server and accepts the certificate with only these two roots. With another root it refuses.

**On a lamp (ESP32-C3, 52 pixels, 25 to 27 November 2026):**

- TLS on the chip: the plan comes in over https every minute, in 3 to 4 seconds. Of the 12 kB stack more
  than 6 kB stays unused.
- Free memory: 150 kB at rest, 111 kB with the link connection open over https (134 kB over http), and
  every minute for a few seconds 57 to 62 kB while the plan is being fetched.
- The default segments: on a clean lamp the firmware makes 14 segments for 52 pixels by itself.
- Settings and info page: settings set through the JSON API; the info page shows the scene, the next
  change, the last fetch and the link code.
- Update over wifi: done three times, the lamp came back with its settings each time.
- Following the plan: a changed plan was on the lamp within a minute (orange to warm white), read from the
  info page.
- Linking via the server: with the service in the local network over http 30 of 30 frames, post to visible
  median 48 ms (one read-out round is 47 ms, the real delay is below that). With the live service over
  https (Caddy, nginx): 40 of 40 frames, median 78 ms, slowest 151 ms, with a read-out round of 53 ms;
  directly to the lamp measured the same way 81 ms. The plan came through every time during five minutes.

## Not tested

- whether the light stutters during fetching, especially on the C3 with one core (only numbers were read,
  nobody looked at the lamp);
- the candle on real pixels;
- the colours themselves (the RGBW values from the plan are still an assumption on the server as well);
- the on/off button in the lamp's web page;
- storing the plan and reading it back on the file system, after a power cut;
- the interaction with a boot preset;
- a plain ESP32 (not the C3): only built, never flashed;
- linking via the server: hours on end, a dropped connection, several lamps at once. One restart seen,
  directly after saving a setting via `/json/cfg` while the memory was at 58 kB; cause not determined.

## Own name and appearance

- **In the build** (`platformio_override.ini`): brand "theDomlamp", product "Domtoren", address `thedomlamp.local`, and
  a setup network with its own name per lamp (`theDomlamp-` plus the last six characters of the serial number).
- **On the lamp** (`ui/install.sh <adres>`): `skin.css` with the colours of the site for WLED's own pages,
  and four presets: Volg de Domtoren, Warm wit, Oranje, Uit. The script overwrites the presets on the lamp.
- Not changed: the WLED logo, the welcome page and the menus. That requires changes in WLED itself.
- Seen on the lamp (25 Nov 2026): name, brand, style sheet and presets are on it; the presets switch
  following on and off. The colours of the presets and the appearance in the browser have not been assessed.
