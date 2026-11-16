# rgbstrip

ESP32-C6 controller for a 4-pin RGB LED strip (`5V`, `G`, `R`, `B`, common anode).
Pick colours from your phone over Wi-Fi or send commands over USB serial.

## Wiring

The strip is driven directly from the ESP32 pins (a colour lights when its pin is pulled LOW):

| Strip pin | ESP32-C6 |
|-----------|----------|
| 5V        | 5V (VBUS) |
| G         | GPIO0    |
| R         | GPIO1    |
| B         | GPIO2    |

Each GPIO can only sink ~20–28 mA, which is fine for a short strip. For a longer strip,
switch each colour through an N-channel logic-level MOSFET and set `ACTIVE_HIGH = true`.

## Build & flash

Arduino IDE (or `arduino-cli`) with the esp32 core 3.x, board **ESP32C6 Dev Module**,
*USB CDC On Boot: Enabled*.

```bash
arduino-cli compile --upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc .
```

## Wi-Fi setup

1. On first boot the board opens its own network **RGB-Strip** (password `ledstrip123`).
2. Connect to it and open `http://192.168.4.1/wifi`, then enter your home Wi-Fi (2.4 GHz only).
3. The credentials are saved on the board and it reboots onto your network.
   Its address is printed on serial (`ip` command); `http://rgbstrip.local` works where mDNS is supported.

If the home network can't be reached at boot, it falls back to the setup network and retries every 5 minutes.

`WIFI_COUNTRY` is set to `"FI"` so channels 12–13 are usable; change it for your country.

## Control

- **Web:** colour picker, Off / White / Rainbow buttons.
- **HTTP:** `/set?hex=RRGGBB`, `/set?r=..&g=..&b=..`, `/rainbow`
- **Serial (115200):** `255 0 128`, `#FF8000`, `off`, `rainbow`, `ip`, `forgetwifi`
