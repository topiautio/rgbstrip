/*
  ESP32-C6 RGB LED strip controller
  Strip pins (in order): 5V, G, R, B  -- common anode, colour lights when pulled LOW.

  Direct wiring (no extra parts):
      Strip 5V -> ESP32 5V (VBUS/5V pin)
      Strip G  -> GPIO0
      Strip R  -> GPIO1
      Strip B  -> GPIO2

  Control:
    * Home Wi-Fi: open http://rgbstrip.local (or the IP printed on serial).
    * First-time setup / home Wi-Fi not reachable: the board opens its own
      network "RGB-Strip" (password "ledstrip123"). Connect to it, open
      http://192.168.4.1/wifi, enter your home Wi-Fi name + password.
      They are saved on the board and it reboots onto your network.
      (ESP32-C6 supports 2.4 GHz Wi-Fi only.)
    * Serial (115200 baud), one command per line:
        255 0 128      -> set R G B (0..255)
        #FF8000        -> set hex colour
        off            -> all off
        rainbow        -> start rainbow fade effect
        ip             -> show network status / address
        forgetwifi     -> erase saved home Wi-Fi and reboot into setup mode
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_wifi.h>

// ---- Configuration -------------------------------------------------------
// Same order as the strip connector: G, R, B
const int PIN_GREEN = 0;
const int PIN_RED   = 1;
const int PIN_BLUE  = 2;

// Strip wired directly: GPIO LOW = colour ON.
// Set to true only if you later add N-channel MOSFETs between ESP32 and strip.
const bool ACTIVE_HIGH = false;

const uint32_t PWM_FREQ = 5000;  // Hz, above flicker range
const uint8_t  PWM_BITS = 8;     // 0..255 duty

const char *AP_SSID = "RGB-Strip";
const char *AP_PASS = "ledstrip123";  // min 8 chars
const char *HOSTNAME = "rgbstrip";    // -> http://rgbstrip.local
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
// Wi-Fi country: enables channels 12-13 (default world mode only uses 1-11)
const char *WIFI_COUNTRY = "FI";

// ---- State ---------------------------------------------------------------
WebServer server(80);
Preferences prefs;
bool setupMode = false;  // true = running own AP for Wi-Fi setup
uint8_t curR = 0, curG = 0, curB = 0;
bool rainbow = false;

// Simple gamma correction so brightness steps look even to the eye
uint8_t gamma8(uint8_t v) {
  return (uint8_t)(powf(v / 255.0f, 2.2f) * 255.0f + 0.5f);
}

void writeChannel(int pin, uint8_t value) {
  uint8_t duty = gamma8(value);
  ledcWrite(pin, ACTIVE_HIGH ? duty : 255 - duty);
}

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  curR = r; curG = g; curB = b;
  writeChannel(PIN_RED, r);
  writeChannel(PIN_GREEN, g);
  writeChannel(PIN_BLUE, b);
}

// Hue 0..359 -> full-saturation RGB
void hueToRgb(uint16_t h, uint8_t &r, uint8_t &g, uint8_t &b) {
  uint8_t region = h / 60;
  uint8_t rem = (h % 60) * 255 / 60;
  switch (region) {
    case 0:  r = 255;       g = rem;       b = 0;         break;
    case 1:  r = 255 - rem; g = 255;       b = 0;         break;
    case 2:  r = 0;         g = 255;       b = rem;       break;
    case 3:  r = 0;         g = 255 - rem; b = 255;       break;
    case 4:  r = rem;       g = 0;         b = 255;       break;
    default: r = 255;       g = 0;         b = 255 - rem; break;
  }
}

bool parseHex(String s, uint8_t &r, uint8_t &g, uint8_t &b) {
  s.trim();
  if (s.startsWith("#")) s.remove(0, 1);
  if (s.length() != 6) return false;
  char *end;
  long v = strtol(s.c_str(), &end, 16);
  if (*end != '\0') return false;
  r = (v >> 16) & 0xFF; g = (v >> 8) & 0xFF; b = v & 0xFF;
  return true;
}

// ---- Web UI --------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RGB Strip</title>
<style>
 body{font-family:sans-serif;background:#111;color:#eee;text-align:center;padding:20px}
 input[type=color]{width:200px;height:200px;border:none;background:none}
 button{font-size:18px;margin:8px;padding:10px 20px;border-radius:8px;border:0}
</style></head><body>
<h2>RGB Strip</h2>
<input type="color" id="c" value="#%COLOR%"><br>
<button onclick="send('/set?hex=000000')">Off</button>
<button onclick="send('/set?hex=FFFFFF')">White</button>
<button onclick="send('/rainbow')">Rainbow</button>
<p><a href="/wifi" style="color:#888">Wi-Fi settings</a></p>
<script>
 let t=null;
 function send(u){fetch(u);}
 document.getElementById('c').addEventListener('input',e=>{
   clearTimeout(t);
   t=setTimeout(()=>send('/set?hex='+e.target.value.substring(1)),40);
 });
</script></body></html>)HTML";

void handleRoot() {
  char hex[7];
  snprintf(hex, sizeof(hex), "%02X%02X%02X", curR, curG, curB);
  String page = FPSTR(INDEX_HTML);
  page.replace("%COLOR%", hex);
  server.send(200, "text/html", page);
}

void handleSet() {
  uint8_t r, g, b;
  if (server.hasArg("hex") && parseHex(server.arg("hex"), r, g, b)) {
    rainbow = false;
    setColor(r, g, b);
    server.send(200, "text/plain", "OK");
  } else if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    rainbow = false;
    setColor(constrain(server.arg("r").toInt(), 0, 255),
             constrain(server.arg("g").toInt(), 0, 255),
             constrain(server.arg("b").toInt(), 0, 255));
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Use /set?hex=RRGGBB or /set?r=&g=&b=");
  }
}

void handleRainbow() {
  rainbow = true;
  server.send(200, "text/plain", "OK");
}

// ---- Wi-Fi setup page ----------------------------------------------------
String htmlEscape(const String &in) {
  String out;
  for (char c : in) {
    switch (c) {
      case '&': out += "&amp;";  break;
      case '<': out += "&lt;";   break;
      case '>': out += "&gt;";   break;
      case '"': out += "&quot;"; break;
      default:  out += c;
    }
  }
  return out;
}

void handleWifiPage() {
  String options;
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    options += "<option value=\"" + htmlEscape(WiFi.SSID(i)) + "\">";
  }
  WiFi.scanDelete();

  String page =
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Wi-Fi setup</title><style>body{font-family:sans-serif;background:#111;color:#eee;"
    "padding:20px;max-width:360px;margin:auto}input,button{font-size:18px;width:100%;"
    "margin:6px 0;padding:8px;box-sizing:border-box}</style></head><body>"
    "<h2>Home Wi-Fi</h2>"
    "<form method='POST' action='/savewifi'>"
    "Network name<input name='ssid' list='nets' required autocomplete='off'>"
    "<datalist id='nets'>" + options + "</datalist>"
    "Password<input name='pass' type='password'>"
    "<button type='submit'>Save &amp; connect</button></form>"
    "<p style='color:#888'>2.4 GHz networks only. The board reboots and joins this network; "
    "then open <b>http://" + String(HOSTNAME) + ".local</b>.</p>"
    "<p><a href='/' style='color:#888'>Back</a></p></body></html>";
  server.send(200, "text/html", page);
}

void handleSaveWifi() {
  String ssid = server.arg("ssid");
  ssid.trim();
  if (ssid.isEmpty()) {
    server.send(400, "text/plain", "Network name is required");
    return;
  }
  prefs.putString("ssid", ssid);
  prefs.putString("pass", server.arg("pass"));
  server.send(200, "text/html",
    "<html><body style='font-family:sans-serif;background:#111;color:#eee;padding:20px'>"
    "Saved. Rebooting and joining <b>" + htmlEscape(ssid) + "</b>...<br>"
    "Reconnect your phone to your home Wi-Fi and open http://" + String(HOSTNAME) + ".local"
    "</body></html>");
  delay(1000);
  ESP.restart();
}

void printNetworkStatus() {
  if (setupMode) {
    Serial.printf("Setup mode: join Wi-Fi '%s' (password '%s'), open http://%s/wifi\n",
                  AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  } else if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected to '%s': http://%s  or  http://%s.local\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), HOSTNAME);
  } else {
    Serial.println("Home Wi-Fi disconnected, reconnecting...");
  }
}

// Try saved home Wi-Fi; fall back to own access point for setup
void startWifi() {
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");

  if (!ssid.isEmpty()) {
    WiFi.mode(WIFI_STA);
    esp_wifi_set_country_code(WIFI_COUNTRY, false);

    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
      Serial.printf("\nDisconnected, reason %d (%s)\n", info.wifi_sta_disconnected.reason,
                    WiFi.disconnectReasonName((wifi_err_reason_t)info.wifi_sta_disconnected.reason));
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    Serial.printf("Connecting to '%s'", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
      delay(500);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      setupMode = false;
      MDNS.begin(HOSTNAME);
      MDNS.addService("http", "tcp", 80);
      return;
    }
    Serial.println("Could not connect to home Wi-Fi.");
  }

  setupMode = true;
  WiFi.mode(WIFI_AP_STA);  // STA part needed for the network scan on /wifi
  esp_wifi_set_country_code(WIFI_COUNTRY, false);
  WiFi.softAP(AP_SSID, AP_PASS);
}

// ---- Serial commands -----------------------------------------------------
void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.isEmpty()) return;

  uint8_t r, g, b;
  int ri, gi, bi;
  if (line.equalsIgnoreCase("off")) {
    rainbow = false; setColor(0, 0, 0);
  } else if (line.equalsIgnoreCase("rainbow")) {
    rainbow = true;
  } else if (line.equalsIgnoreCase("ip")) {
    printNetworkStatus();
    return;
  } else if (line.equalsIgnoreCase("forgetwifi")) {
    prefs.remove("ssid");
    prefs.remove("pass");
    Serial.println("Saved Wi-Fi erased, rebooting into setup mode...");
    delay(200);
    ESP.restart();
  } else if (parseHex(line, r, g, b)) {
    rainbow = false; setColor(r, g, b);
  } else if (sscanf(line.c_str(), "%d %d %d", &ri, &gi, &bi) == 3) {
    rainbow = false;
    setColor(constrain(ri, 0, 255), constrain(gi, 0, 255), constrain(bi, 0, 255));
  } else {
    Serial.println("Commands: 'R G B' | '#RRGGBB' | off | rainbow | ip | forgetwifi");
    return;
  }
  Serial.printf("Color: %u %u %u%s\n", curR, curG, curB, rainbow ? " (rainbow)" : "");
}

// ---- Setup / loop --------------------------------------------------------
void setup() {
  Serial.begin(115200);

  ledcAttach(PIN_RED,   PWM_FREQ, PWM_BITS);
  ledcAttach(PIN_GREEN, PWM_FREQ, PWM_BITS);
  ledcAttach(PIN_BLUE,  PWM_FREQ, PWM_BITS);
  setColor(0, 0, 0);

  // Quick startup test: red, green, blue
  setColor(255, 0, 0); delay(400);
  setColor(0, 255, 0); delay(400);
  setColor(0, 0, 255); delay(400);
  setColor(0, 0, 0);

  prefs.begin("wifi", false);
  startWifi();
  printNetworkStatus();

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/rainbow", handleRainbow);
  server.on("/wifi", HTTP_GET, handleWifiPage);
  server.on("/savewifi", HTTP_POST, handleSaveWifi);
  server.begin();
}

void loop() {
  server.handleClient();
  handleSerial();

  // In setup mode with saved credentials (e.g. router was off at boot):
  // retry home Wi-Fi every 5 minutes unless someone is using the setup AP.
  if (setupMode && prefs.isKey("ssid")) {
    static uint32_t since = millis();
    if (WiFi.softAPgetStationNum() > 0) since = millis();
    if (millis() - since > 5UL * 60 * 1000) ESP.restart();
  }

  if (rainbow) {
    static uint16_t hue = 0;
    static uint32_t last = 0;
    if (millis() - last >= 20) {
      last = millis();
      uint8_t r, g, b;
      hueToRgb(hue, r, g, b);
      setColor(r, g, b);
      hue = (hue + 1) % 360;
    }
  }
}
