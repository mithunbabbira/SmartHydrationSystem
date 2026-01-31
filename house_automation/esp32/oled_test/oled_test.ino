/*
 * ONO Display - ESP32 + 0.91" Blue OLED
 * WiFi: ONO price (CoinGecko), Pi health check
 * ESP-NOW: receive text, rainbow, color from Pi dashboard
 *
 * Wiring:
 *   Display (VCC, GND, SCK, SDA): VCC->3.3V GND->GND SCK->22 SDA->21
 *   RGB (common anode): RED->27 GREEN->14 BLUE->12
 *
 * Protocol (Type 3): 0x50=rainbow, 0x51=color, 0x60=text
 * Libraries: Adafruit GFX, Adafruit SSD1306, ArduinoJson
 */

#include <Wire.h>
#include <string.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C  // Try 0x3D if display doesn't work

#define RGB_RED    27
#define RGB_GREEN  14
#define RGB_BLUE   12

#define PRICE_DISPLAY_MS   3000  // Show price for 3 sec
#define CHANGE_DISPLAY_MS  1000  // Show 24h change for 1 sec

#define RGB_DIM_LEVEL     20    // Dim green/red (0-255) – very dim
#define PI_CHECK_MS       30000 // Check Pi health every 30 sec

const char* ssid     = "No 303";
const char* password = "3.14159265";

const char* apiUrl = "https://api.coingecko.com/api/v3/simple/price?ids=onocoy-token&vs_currencies=usd,inr&include_24hr_change=true";
const char* piHealthUrl = "http://raspberrypi.local:5000/api/health?system=true";

#define FETCH_INTERVAL_MS 60000  // CoinGecko free tier ~10/min – fetch every 60s
#define MAX_TEXT_LEN 81
#define SCROLL_SPEED_MS 150

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float priceUsd  = 0;
float priceInr  = 0;
float change24h = 0;
bool  dataValid = false;
unsigned long lastFetch = 0;
unsigned long displaySwitchAt = 0;
unsigned long lastPiCheck = 0;
bool  piOk = true;
bool showingPrice = true;

// Pi override (from ESP-NOW)
unsigned long overrideUntil = 0;
bool overrideRainbow = false;
uint8_t overrideR = 0, overrideG = 0, overrideB = 0;
char overrideText[MAX_TEXT_LEN] = "";
bool overrideTextMode = false;

void OnEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < 2 || data[0] != 3) return;

  uint8_t cmd = data[1];
  unsigned long durationMs = 5000;

  if (cmd == 0x50 && len >= 6) {
    float sec;
    memcpy(&sec, &data[2], 4);
    durationMs = (unsigned long)(sec * 1000);
    overrideUntil = millis() + durationMs;
    overrideRainbow = true;
    overrideTextMode = false;
    Serial.printf("ONO Rainbow %lus\n", (unsigned long)sec);
  } else if (cmd == 0x51 && len >= 8) {
    float sec;
    memcpy(&sec, &data[5], 4);
    durationMs = (unsigned long)(sec * 1000);
    overrideUntil = millis() + durationMs;
    overrideRainbow = false;
    overrideR = data[2]; overrideG = data[3]; overrideB = data[4];
    overrideTextMode = false;
    Serial.printf("ONO Color R%d G%d B%d %lus\n", overrideR, overrideG, overrideB, (unsigned long)sec);
  } else if (cmd == 0x60 && len >= 9) {
    float sec;
    memcpy(&sec, &data[2], 4);
    durationMs = (unsigned long)(sec * 1000);
    int txtLen = data[6];
    if (txtLen > 0 && txtLen < MAX_TEXT_LEN && len >= 7 + txtLen) {
      memcpy(overrideText, &data[7], txtLen);
      overrideText[txtLen] = '\0';
      overrideUntil = millis() + durationMs;
      overrideRainbow = false;
      overrideTextMode = true;
      Serial.printf("ONO Text '%s' %lus\n", overrideText, (unsigned long)sec);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RGB_RED, OUTPUT);
  pinMode(RGB_GREEN, OUTPUT);
  pinMode(RGB_BLUE, OUTPUT);
  rgbOff();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED init failed - check wiring or try 0x3D");
    while (1) delay(10);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    showError("WiFi failed!");
    while (1) delay(1000);
  }

  Serial.println("WiFi OK");
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
  } else {
    esp_now_register_recv_cb(OnEspNowRecv);
    Serial.println("ESP-NOW ready");
  }

  fetchOnoPrice();
}

void fetchOnoPrice() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, apiUrl)) {
    Serial.println("HTTP begin failed");
    return;
  }

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", httpCode);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return;
  }

  JsonObject ono = doc["onocoy-token"];
  if (ono.isNull()) {
    Serial.println("onocoy-token not found in response");
    return;
  }

  priceUsd  = ono["usd"].as<float>();
  priceInr  = ono["inr"].as<float>();
  change24h = ono["usd_24h_change"].as<float>();
  dataValid = true;

  Serial.printf("ONO: $%.6f  Rs%.4f  24h: %+.2f%%\n", priceUsd, priceInr, change24h);
}

void showError(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 8);
  display.println(msg);
  display.display();
  Serial.println(msg);
}

void showPiDownScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 12);
  display.println("pi not working");
  display.display();
}

bool checkPiHealth() {
  HTTPClient http;
  if (!http.begin(piHealthUrl)) return false;
  http.setTimeout(5000);
  int code = http.GET();
  String payload = http.getString();
  http.end();

  if (code != 200) return false;

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload)) return false;

  bool ok = doc["ok"].as<bool>();
  const char* ctrl = doc["controller"].as<const char*>();
  bool serialOk = doc["serial"]["serial_connected"].as<bool>();

  return ok && ctrl && strcmp(ctrl, "running") == 0 && serialOk;
}

void rgbRainbow() {
  unsigned long t = millis() / 80;
  int phase = (t % 7);
  int r = 0, g = 0, b = 0;
  switch (phase) {
    case 0: r = 255; g = 0;   b = 0;   break;
    case 1: r = 255; g = 128; b = 0;   break;
    case 2: r = 255; g = 255; b = 0;   break;
    case 3: r = 0;   g = 255; b = 0;   break;
    case 4: r = 0;   g = 255; b = 255; break;
    case 5: r = 0;   g = 0;   b = 255; break;
    case 6: r = 255; g = 0;   b = 255; break;
    default: r = 255; g = 0; b = 0; break;
  }
  rgbSetColor(r, g, b);
}

void rgbOff() {
  analogWrite(RGB_RED, 255);
  analogWrite(RGB_GREEN, 255);
  analogWrite(RGB_BLUE, 255);
}

// Common anode: 255=off, 0=full on
void rgbSetColor(int r, int g, int b) {
  analogWrite(RGB_RED, 255 - r);
  analogWrite(RGB_GREEN, 255 - g);
  analogWrite(RGB_BLUE, 255 - b);
}

void drawValue(const char* buf, int len) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  int numSize = (len <= 6) ? 3 : 2;
  int charW = (numSize == 3) ? 18 : 12;
  int numH = (numSize == 3) ? 24 : 16;
  int numW = len * charW;
  int xStart = (128 - numW) / 2;
  int yNum = (32 - numH) / 2;

  display.setTextSize(numSize);
  display.setCursor(xStart, yNum);
  display.print(buf);
  display.display();
}

void showPriceScreen() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.4f", priceUsd);
  drawValue(buf, strlen(buf));
}

void showChangeScreen() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%+.2f%%", change24h);
  drawValue(buf, strlen(buf));
}

void showScrollingText(const char* txt) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.setTextSize(1);
  int txtLen = strlen(txt);
  int charW = 6;
  int totalW = txtLen * charW;
  if (totalW <= SCREEN_WIDTH) {
    display.setCursor((SCREEN_WIDTH - totalW) / 2, 12);
    display.print(txt);
  } else {
    int pos = (millis() / SCROLL_SPEED_MS) % (totalW + 30);
    int x = SCREEN_WIDTH - pos;
    display.setCursor(x, 12);
    display.print(txt);
    display.setCursor(x + totalW + 30, 12);
    display.print(txt);
  }
  display.display();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    rgbOff();
    showError("Reconnecting...");
    delay(2000);
    return;
  }

  // Pi override from ESP-NOW (text, rainbow, color)
  if (overrideUntil > millis()) {
    if (overrideTextMode) {
      rgbOff();
      showScrollingText(overrideText);
    } else if (overrideRainbow) {
      rgbRainbow();
      display.clearDisplay();
      display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(0, 12);
      display.print("Rainbow");
      display.display();
    } else {
      rgbSetColor(overrideR, overrideG, overrideB);
      display.clearDisplay();
      display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
      display.display();
    }
    delay(50);
    return;
  }

  // Pi health check
  if (millis() - lastPiCheck >= PI_CHECK_MS || lastPiCheck == 0) {
    lastPiCheck = millis();
    piOk = checkPiHealth();
    if (!piOk) Serial.println("Pi health check failed");
  }

  // Pi not working: rainbow + "pi not working" display
  if (!piOk) {
    rgbRainbow();
    showPiDownScreen();
    delay(100);
    return;
  }

  // Pi OK: normal flow
  if (millis() - lastFetch >= FETCH_INTERVAL_MS || lastFetch == 0) {
    lastFetch = millis();
    fetchOnoPrice();
    if (dataValid) {
      displaySwitchAt = millis();
      showingPrice = true;
      showPriceScreen();
      // Dim green when up, dim red when down
      rgbSetColor(change24h >= 0 ? 0 : RGB_DIM_LEVEL, change24h >= 0 ? RGB_DIM_LEVEL : 0, 0);
    }
  }

  if (dataValid) {
    unsigned long elapsed = millis() - displaySwitchAt;
    if (elapsed >= PRICE_DISPLAY_MS + CHANGE_DISPLAY_MS) {
      displaySwitchAt = millis();
      if (!showingPrice) { showingPrice = true; showPriceScreen(); }
    } else if (elapsed >= PRICE_DISPLAY_MS) {
      if (showingPrice) { showingPrice = false; showChangeScreen(); }
    }
  } else {
    rgbOff();
    showError("No price data");
  }

  delay(250);
}
