// Water Lilies — Agent-native IoT Display
// Monet Ltd. 2026
//
// Hardware: ESP32-C3 Super Mini + SSD1306 128x64 I2C OLED
// Wiring:   GPIO5=SCL, GPIO6=SDA

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>

// --- Hardware config ---
#define SDA_PIN 6
#define SCL_PIN 5
#define SCREEN_W 128
#define SCREEN_H 64

// --- Objects ---
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN);
WebServer server(80);
Preferences prefs;

// --- State ---
String authToken = "";
String wifiSSID = "";
String wifiPass = "";
bool wifiConnected = false;

// --- Serial protocol ---
#define SERIAL_END_MARKER "\n---END---"

void serialRespond(const String& msg) {
  Serial.println(msg);
  Serial.println("---END---");
}

// --- Display helpers ---
const uint8_t* getFontBySize(int size) {
  if (size >= 24) return u8g2_font_profont22_tr;
  if (size >= 16) return u8g2_font_profont17_tr;
  if (size >= 12) return u8g2_font_profont12_tr;
  if (size >= 10) return u8g2_font_profont11_tr;
  return u8g2_font_profont10_tr;
}

void drawFromJson(const String& json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    serialRespond("ERR:INVALID_JSON " + String(err.c_str()));
    return;
  }

  u8g2.clearBuffer();

  JsonArray drawArr = doc["draw"].as<JsonArray>();
  for (JsonObject cmd : drawArr) {
    const char* type = cmd["type"] | "";
    int x = cmd["x"] | 0;
    int y = cmd["y"] | 0;

    if (strcmp(type, "text") == 0) {
      int size = cmd["size"] | 12;
      const char* text = cmd["text"] | "";
      const char* align = cmd["align"] | "left";
      u8g2.setFont(getFontBySize(size));
      u8g2.setDrawColor(1);
      if (strcmp(align, "right") == 0) {
        int w = u8g2.getStrWidth(text);
        u8g2.drawStr(x - w, y, text);
      } else {
        u8g2.drawStr(x, y, text);
      }
    }
    else if (strcmp(type, "rect") == 0) {
      int w = cmd["w"] | 10;
      int h = cmd["h"] | 10;
      bool fill = cmd["fill"] | false;
      if (fill) u8g2.drawBox(x, y, w, h);
      else u8g2.drawFrame(x, y, w, h);
    }
    else if (strcmp(type, "bar") == 0) {
      int w = cmd["w"] | SCREEN_W;
      int h = cmd["h"] | 8;
      float pct = cmd["pct"] | 0.0;
      u8g2.drawFrame(x, y, w, h);
      int fillW = (int)(w * pct / 100.0);
      if (fillW > 0) u8g2.drawBox(x, y, fillW, h);
    }
    else if (strcmp(type, "line") == 0) {
      int x2 = cmd["x2"] | 0;
      int y2 = cmd["y2"] | 0;
      u8g2.drawLine(x, y, x2, y2);
    }
    else if (strcmp(type, "pixel") == 0) {
      u8g2.drawPixel(x, y);
    }
    else if (strcmp(type, "circle") == 0) {
      int r = cmd["r"] | 5;
      bool fill = cmd["fill"] | false;
      if (fill) u8g2.drawDisc(x, y, r);
      else u8g2.drawCircle(x, y, r);
    }
  }

  u8g2.sendBuffer();
}

// --- /info content ---
String getInfoText() {
  String info = "# Water Lilies Display MVP\n";
  info += "## 128x64 Monochrome OLED (SSD1306, I2C)\n";

  if (!wifiConnected) {
    info += "## Current Mode: USB Serial (115200 baud)\n\n";
    info += "This device is not yet connected to WiFi.\n\n";
    info += "## Quick Start\n";
    info += "  1. POST /wifi {\"ssid\":\"YourNetwork\",\"pass\":\"YourPassword\"}\n";
    info += "     -> Returns \"OK 192.168.x.x\" on success\n";
    info += "     -> Error codes: ERR:SSID_NOT_FOUND, ERR:AUTH_FAILED, ERR:DHCP_TIMEOUT\n";
    info += "  2. POST /provision\n";
    info += "     -> Returns \"TOKEN <token>\" -- save this for HTTP authentication\n\n";
    info += "## Available Commands (Serial)\n";
    info += "  GET /info              -> This help text\n";
    info += "  GET /status            -> Device status (JSON)\n";
    info += "  POST /wifi {...}       -> Connect to WiFi\n";
    info += "  POST /provision        -> Generate auth token\n";
    info += "  POST /display {...}    -> Draw (same JSON format as HTTP mode)\n";
    info += "  POST /clear            -> Clear screen\n";
    info += "  POST /factory-reset    -> Wipe all stored credentials\n\n";
    info += "## Serial Protocol\n";
    info += "  Request: one line ending with \\n (no newlines in JSON body)\n";
    info += "  Response: ends with \\n---END---\\n\n\n";
    info += "After WiFi is connected, HTTP server starts. Serial remains available.\n";
  } else {
    info += "## Current Mode: WiFi HTTP + Serial\n";
    info += "## IP: " + WiFi.localIP().toString() + "\n\n";
    info += "## Authentication\n";
    info += "All POST requests require: Authorization: Bearer <token>\n";
    info += "Token is generated via serial: POST /provision\n";
    info += "GET requests (/info, /status) do not require auth.\n\n";
    info += "## Endpoints\n";
    info += "  GET  /info        -> This help text (full, for setup)\n";
    info += "  GET  /status      -> Device status JSON (lightweight)\n";
    info += "  POST /display     -> Draw shapes and text (JSON, max 4KB)\n";
    info += "  POST /clear       -> Clear screen\n\n";
    info += "## Drawing Types\n";
    info += "  text:   { \"type\":\"text\", \"x\":0, \"y\":12, \"text\":\"hello\", \"size\":12 }\n";
    info += "  rect:   { \"type\":\"rect\", \"x\":0, \"y\":0, \"w\":50, \"h\":20, \"fill\":true }\n";
    info += "  bar:    { \"type\":\"bar\", \"x\":0, \"y\":56, \"w\":128, \"h\":8, \"pct\":75 }\n";
    info += "  line:   { \"type\":\"line\", \"x1\":0, \"y1\":0, \"x2\":127, \"y2\":63 }\n";
    info += "  circle: { \"type\":\"circle\", \"x\":64, \"y\":32, \"r\":10, \"fill\":false }\n";
    info += "  pixel:  { \"type\":\"pixel\", \"x\":10, \"y\":10 }\n\n";
    info += "Screen is monochrome -- no color parameter needed, all pixels are white-on-black.\n";
    info += "Supported text sizes: 8, 10, 12, 16, 24\n";
    info += "Screen is 128 wide x 64 tall. Y=0 is top.\n";
    info += "Note: for text, y is the BASELINE, not the top. A size-12 text at y=12 starts near the top.\n\n";
    info += "## Example\n";
    info += "  curl -s -m 1 -X POST \"http://" + WiFi.localIP().toString() + "/display\" \\\n";
    info += "    -H \"Authorization: Bearer YOUR_TOKEN\" \\\n";
    info += "    -H \"Content-Type: application/json\" \\\n";
    info += "    -d '{\"draw\":[{\"type\":\"text\",\"x\":0,\"y\":12,\"text\":\"5h 73%\",\"size\":16},{\"type\":\"bar\",\"x\":0,\"y\":20,\"w\":128,\"h\":8,\"pct\":73}]}'\n";
  }
  return info;
}

String getStatusJson() {
  JsonDocument doc;
  doc["device"] = "Water Lilies Display MVP";
  doc["api_version"] = "0.1.0";
  JsonObject screen = doc["screen"].to<JsonObject>();
  screen["w"] = SCREEN_W;
  screen["h"] = SCREEN_H;
  screen["type"] = "monochrome";
  doc["mode"] = wifiConnected ? "wifi" : "serial";
  if (wifiConnected) {
    doc["ip"] = WiFi.localIP().toString();
    doc["wifi_rssi"] = WiFi.RSSI();
  }
  doc["uptime_s"] = millis() / 1000;
  doc["free_heap"] = ESP.getFreeHeap();

  JsonArray caps = doc["capabilities"].to<JsonArray>();
  caps.add("display");
  caps.add("clear");

  JsonArray eps = doc["endpoints"].to<JsonArray>();
  eps.add("POST /display");
  eps.add("POST /clear");

  String out;
  serializeJsonPretty(doc, out);
  return out;
}

// --- Auth check ---
bool checkAuth() {
  if (authToken.isEmpty()) return true; // no token set yet
  String header = server.header("Authorization");
  if (header.startsWith("Bearer ")) {
    return header.substring(7) == authToken;
  }
  return false;
}

// --- HTTP handlers ---
void handleInfo() {
  server.send(200, "text/plain", getInfoText());
}

void handleStatus() {
  server.send(200, "application/json", getStatusJson());
}

void handleDisplay() {
  if (!checkAuth()) {
    server.send(401, "text/plain", "ERR:UNAUTHORIZED");
    return;
  }
  String body = server.arg("plain");
  drawFromJson(body);
  server.send(200, "text/plain", "OK");
}

void handleClear() {
  if (!checkAuth()) {
    server.send(401, "text/plain", "ERR:UNAUTHORIZED");
    return;
  }
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  server.send(200, "text/plain", "OK");
}

void startHttpServer() {
  const char* headerKeys[] = {"Authorization"};
  server.collectHeaders(headerKeys, 1);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/display", HTTP_POST, handleDisplay);
  server.on("/clear", HTTP_POST, handleClear);
  server.begin();
}

// --- WiFi connection ---
bool connectWiFi(const String& ssid, const String& pass, int timeoutSec) {
  WiFi.disconnect(true);
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  int elapsed = 0;
  while (WiFi.status() != WL_CONNECTED && elapsed < timeoutSec * 10) {
    delay(100);
    elapsed++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    return true;
  }
  return false;
}

// --- Token generation ---
String generateToken() {
  String token = "";
  const char chars[] = "abcdef0123456789";
  for (int i = 0; i < 16; i++) {
    token += chars[random(0, 16)];
  }
  return token;
}

// --- Serial command handler ---
void handleSerial() {
  if (!Serial.available()) return;

  // Read until newline, handling large payloads (up to 1KB)
  String line = "";
  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') break;
      line += c;
      start = millis(); // reset timeout on each byte
    }
  }
  line.trim();
  if (line.isEmpty()) return;

  if (line == "GET /info") {
    serialRespond(getInfoText());
  }
  else if (line == "GET /status") {
    serialRespond(getStatusJson());
  }
  else if (line.startsWith("POST /wifi ")) {
    String json = line.substring(11);
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
      serialRespond("ERR:INVALID_JSON");
      return;
    }
    String ssid = doc["ssid"] | "";
    String pass = doc["pass"] | "";
    if (ssid.isEmpty()) {
      serialRespond("ERR:INVALID_JSON missing ssid");
      return;
    }

    // Show on screen
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_profont11_tr);
    u8g2.drawStr(0, 12, "Connecting...");
    u8g2.drawStr(0, 28, ssid.c_str());
    u8g2.sendBuffer();

    if (connectWiFi(ssid, pass, 20)) {
      // Save credentials
      prefs.begin("wl", false);
      prefs.putString("ssid", ssid);
      prefs.putString("pass", pass);
      prefs.end();

      wifiSSID = ssid;
      wifiPass = pass;
      startHttpServer();

      // Show IP on screen
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_profont12_tr);
      u8g2.drawStr(0, 12, "Connected!");
      u8g2.setFont(u8g2_font_profont17_tr);
      u8g2.drawStr(0, 36, WiFi.localIP().toString().c_str());
      u8g2.setFont(u8g2_font_profont10_tr);
      u8g2.drawStr(0, 52, "Serial still available");
      u8g2.sendBuffer();

      serialRespond("OK " + WiFi.localIP().toString());
    } else {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(0, 12, "WiFi failed");
      u8g2.drawStr(0, 28, "Serial ready");
      u8g2.sendBuffer();

      wl_status_t st = WiFi.status();
      if (st == WL_NO_SSID_AVAIL) serialRespond("ERR:SSID_NOT_FOUND");
      else if (st == WL_CONNECT_FAILED) serialRespond("ERR:AUTH_FAILED");
      else serialRespond("ERR:DHCP_TIMEOUT");
    }
  }
  else if (line == "POST /provision") {
    authToken = generateToken();
    prefs.begin("wl", false);
    prefs.putString("token", authToken);
    prefs.end();
    serialRespond("TOKEN " + authToken);
  }
  else if (line.startsWith("POST /display ")) {
    String json = line.substring(14);
    drawFromJson(json);
    serialRespond("OK");
  }
  else if (line == "POST /clear") {
    u8g2.clearBuffer();
    u8g2.sendBuffer();
    serialRespond("OK");
  }
  else if (line == "POST /factory-reset") {
    prefs.begin("wl", false);
    prefs.clear();
    prefs.end();
    authToken = "";
    wifiSSID = "";
    wifiPass = "";
    WiFi.disconnect();
    wifiConnected = false;
    serialRespond("OK factory reset done, reboot recommended");
  }
  else {
    serialRespond("ERR:UNKNOWN_CMD " + line);
  }
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(5000); // allow longer serial reads for JSON payloads
  delay(500);

  // Init display
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont12_tr);
  u8g2.drawStr(0, 14, "Water Lilies");
  u8g2.setFont(u8g2_font_profont11_tr);
  u8g2.drawStr(0, 30, "MVP v0.1.0");

  // Load saved credentials
  prefs.begin("wl", true);
  wifiSSID = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  authToken = prefs.getString("token", "");
  prefs.end();

  if (!wifiSSID.isEmpty()) {
    u8g2.drawStr(0, 46, "Connecting WiFi...");
    u8g2.sendBuffer();

    if (connectWiFi(wifiSSID, wifiPass, 10)) {
      startHttpServer();
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_profont12_tr);
      u8g2.drawStr(0, 14, "Water Lilies");
      u8g2.setFont(u8g2_font_profont17_tr);
      u8g2.drawStr(0, 36, WiFi.localIP().toString().c_str());
      u8g2.setFont(u8g2_font_profont10_tr);
      u8g2.drawStr(0, 52, "HTTP + Serial ready");
      u8g2.sendBuffer();
    } else {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_profont12_tr);
      u8g2.drawStr(0, 14, "Water Lilies");
      u8g2.setFont(u8g2_font_profont11_tr);
      u8g2.drawStr(0, 30, "WiFi failed");
      u8g2.drawStr(0, 46, "Serial ready");
      u8g2.sendBuffer();
    }
  } else {
    u8g2.drawStr(0, 46, "Setup via USB...");
    u8g2.sendBuffer();
  }

  Serial.println("Water Lilies MVP ready");
  Serial.println("Send 'GET /info' for help");
}

// --- Loop ---
void loop() {
  handleSerial();
  if (wifiConnected) {
    server.handleClient();
  }
}
