/*
 * CAM Display - ESP32-CAM + 0.91" I2C OLED + RGB LED
 *
 * Same protocol as oled_test: Pi sends price (0x70), text (0x60), rainbow (0x50), color (0x51).
 * Pi health: no packet for 90s -> "PI down" + rainbow.
 *
 * Board: ESP32-CAM (AI-Thinker) or ESP32 Dev Module (if PSRAM boot loop)
 *
 * Wiring (original working config):
 *   OLED: VCC->3V3  GND->GND  SDA->GPIO2  SCL->GPIO14
 *   RGB (common anode): Red->GPIO12  Green->GPIO13  Blue->GPIO15  Common->3V3
 *   Add ~220Ω resistor per RGB pin (or cap RGB to 30 in code if no resistor).
 *
 * If boot loop: add 10k resistor from GPIO 2 to GND (keeps it LOW at boot).
 *
 * Auto-recovery: task watchdog reboots if setup/loop hangs (e.g. WiFi/ESP-NOW
 * stuck after "Starting..."). OLED init failure also reboots after a short wait.
 */

#include <Wire.h>
#include <string.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_task_wdt.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  32
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C

#define SDA_PIN  2
#define SCL_PIN  14
#define RGB_RED    12
#define RGB_GREEN  13
#define RGB_BLUE   15

#define PRICE_DISPLAY_MS  3000
#define CHANGE_DISPLAY_MS 1000
#define RGB_DIM_LEVEL     20
#define RGB_RAINBOW_LEVEL 25   // Dim rainbow to save power
#define NO_DATA_MS        90000
#define MAX_TEXT_LEN      81
#define SCROLL_SPEED_MS   120

// Reboot if loop/setup does not feed WDT within this window (covers WiFi hang)
#define WDT_TIMEOUT_SEC       20
#define OLED_FAIL_REBOOT_MS   5000
#define WIFI_INIT_RETRIES     3

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float priceUsd = 0;
float change24h = 0;
bool dataValid = false;
unsigned long lastPriceUpdate = 0;
unsigned long lastPiActivity = 0;
unsigned long displaySwitchAt = 0;
bool showingPrice = true;

unsigned long overrideUntil = 0;
bool overrideRainbow = false;
uint8_t overrideR = 0, overrideG = 0, overrideB = 0;
char overrideText[MAX_TEXT_LEN] = "";
bool overrideTextMode = false;

bool oledReady = false;

static void wdtFeed() {
  esp_task_wdt_reset();
}

static void wdtBegin() {
  // Ensure a clean WDT config that panics (reboots) on timeout
  esp_task_wdt_deinit();
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  esp_task_wdt_config_t cfg = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_task_wdt_init(&cfg);
#else
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif
  esp_task_wdt_add(NULL);
}

static void rebootSoon(const char* reason, unsigned long waitMs) {
  Serial.printf("REBOOT: %s (in %lu ms)\n", reason ? reason : "unknown", waitMs);
  if (oledReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 4);
    display.print("Rebooting...");
    display.setCursor(0, 16);
    if (reason) display.print(reason);
    display.display();
  }
  unsigned long start = millis();
  while (millis() - start < waitMs) {
    wdtFeed();
    delay(50);
  }
  ESP.restart();
}

static void showBootStatus(const char* msg) {
  if (!oledReady || !msg) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 12);
  display.print(msg);
  display.display();
}

void OnEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (!data || len < 2 || data[0] != 3) return;
  uint8_t cmd = data[1];
  lastPiActivity = millis();

  if (cmd == 0x70 && len >= 10) {
    memcpy(&priceUsd, &data[2], 4);
    memcpy(&change24h, &data[6], 4);
    dataValid = true;
    lastPriceUpdate = millis();
    displaySwitchAt = millis();
    showingPrice = true;
    Serial.printf("ONO Price $%.4f 24h %+.2f%%\n", priceUsd, change24h);
    return;
  }

  if (cmd == 0x50 && len >= 6) {
    float sec;
    memcpy(&sec, &data[2], 4);
    overrideUntil = millis() + (unsigned long)(sec * 1000);
    overrideRainbow = true;
    overrideTextMode = false;
  } else if (cmd == 0x51 && len >= 8) {
    float sec;
    memcpy(&sec, &data[5], 4);
    overrideUntil = millis() + (unsigned long)(sec * 1000);
    overrideRainbow = false;
    overrideR = (data[2] > 30) ? 30 : data[2];
    overrideG = (data[3] > 30) ? 30 : data[3];
    overrideB = (data[4] > 30) ? 30 : data[4];
    overrideTextMode = false;
  } else if (cmd == 0x60 && len >= 9) {
    float sec;
    memcpy(&sec, &data[2], 4);
    int txtLen = data[6];
    if (txtLen > 0 && txtLen < MAX_TEXT_LEN && len >= 7 + txtLen) {
      memcpy(overrideText, &data[7], txtLen);
      overrideText[txtLen] = '\0';
      for (int i = 0; i < txtLen; i++)
        if (overrideText[i] == '\n' || overrideText[i] == '\r') overrideText[i] = ' ';
      overrideUntil = millis() + (unsigned long)(sec * 1000);
      overrideRainbow = false;
      overrideTextMode = true;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Start watchdog early so a hang in WiFi/ESP-NOW auto-reboots
  wdtBegin();
  wdtFeed();

  pinMode(RGB_RED, OUTPUT);
  pinMode(RGB_GREEN, OUTPUT);
  pinMode(RGB_BLUE, OUTPUT);
  rgbOff();
  wdtFeed();

  delay(100);
  Wire.begin(SDA_PIN, SCL_PIN);
  wdtFeed();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED init failed - check SDA/SCL; will reboot");
    // Do not spin forever — soft reboot so a flaky power/I2C bus can recover
    rebootSoon("OLED fail", OLED_FAIL_REBOOT_MS);
  }
  oledReady = true;

  showBootStatus("Starting...");
  wdtFeed();

  // WiFi/ESP-NOW can hang on ESP32-CAM; retry a few times, then let WDT reboot
  bool espNowOk = false;
  for (int attempt = 1; attempt <= WIFI_INIT_RETRIES; attempt++) {
    wdtFeed();
    char msg[24];
    snprintf(msg, sizeof(msg), "WiFi %d/%d...", attempt, WIFI_INIT_RETRIES);
    showBootStatus(msg);
    Serial.printf("WiFi/ESP-NOW init attempt %d/%d\n", attempt, WIFI_INIT_RETRIES);

    WiFi.mode(WIFI_STA);
    wdtFeed();
    WiFi.disconnect(true, true);
    delay(100);
    wdtFeed();

    if (esp_now_init() == ESP_OK) {
      esp_now_register_recv_cb(OnEspNowRecv);
      espNowOk = true;
      Serial.println("ESP-NOW ready");
      break;
    }
    Serial.println("ESP-NOW init failed, retrying...");
    esp_now_deinit();
    WiFi.mode(WIFI_OFF);
    delay(300);
    wdtFeed();
  }

  if (!espNowOk) {
    rebootSoon("ESP-NOW fail", 2000);
  }

  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
  showBootStatus("Ready");
  delay(400);
  wdtFeed();
}

void showPiDownScreen() {
  const char* txt = "PI down";
  int len = 7;
  int textSize = 3;
  int charW = 18, textH = 24;
  int totalW = len * charW;
  int x = (SCREEN_WIDTH - totalW) / 2;
  int y = (SCREEN_HEIGHT - textH) / 2;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.setTextSize(textSize);
  display.setCursor(x, y);
  display.print(txt);
  display.display();
}

void showNoDataScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 12);
  display.print("no data");
  display.display();
}

void rgbRainbow() {
  unsigned long t = millis() / 80;
  int phase = (t % 7);
  int r = 0, g = 0, b = 0;
  int m = RGB_RAINBOW_LEVEL;  // Dim to save power
  switch (phase) {
    case 0: r = m;   g = 0;   b = 0;   break;
    case 1: r = m;   g = m/2; b = 0;   break;
    case 2: r = m;   g = m;   b = 0;   break;
    case 3: r = 0;   g = m;   b = 0;   break;
    case 4: r = 0;   g = m;   b = m;   break;
    case 5: r = 0;   g = 0;   b = m;   break;
    case 6: r = m;   g = 0;   b = m;   break;
    default: r = m; g = 0; b = 0; break;
  }
  rgbSetColor(r, g, b);
}

void rgbOff() {
  analogWrite(RGB_RED, 255);
  analogWrite(RGB_GREEN, 255);
  analogWrite(RGB_BLUE, 255);
}

void rgbSetColor(int r, int g, int b) {
  analogWrite(RGB_RED, 255 - r);
  analogWrite(RGB_GREEN, 255 - g);
  analogWrite(RGB_BLUE, 255 - b);
}

void drawValue(const char* buf, int len) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  int numSize = (len <= 6) ? 3 : 2;
  int charW = (numSize == 3) ? 18 : 12;
  int numH = (numSize == 3) ? 24 : 16;
  int numW = len * charW;
  int xStart = (SCREEN_WIDTH - numW) / 2;
  int yNum = (SCREEN_HEIGHT - numH) / 2;
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

void showPiText(const char* txt) {
  if (!txt) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  int txtLen = (int)strlen(txt);
  if (txtLen <= 0) { display.display(); return; }

  int textSize = (txtLen <= 6) ? 3 : 2;
  int charW = (textSize == 3) ? 18 : 12;
  int textH = (textSize == 3) ? 24 : 16;
  int totalW = txtLen * charW;
  int y = (SCREEN_HEIGHT - textH) / 2;

  display.setTextSize(textSize);
  if (totalW <= SCREEN_WIDTH) {
    int x = (SCREEN_WIDTH - totalW) / 2;
    display.setCursor(x, y);
    display.print(txt);
  } else {
    int pos = (millis() / SCROLL_SPEED_MS) % (totalW + 40);
    int x = SCREEN_WIDTH - pos;
    display.setCursor(x, y);
    display.print(txt);
    display.setCursor(x + totalW + 40, y);
    display.print(txt);
  }
  display.display();
}

void loop() {
  wdtFeed();  // must run every loop — if anything hangs, WDT reboots

  if (overrideUntil > millis()) {
    if (overrideTextMode) {
      rgbOff();
      showPiText(overrideText);
    } else if (overrideRainbow) {
      rgbRainbow();
      display.clearDisplay();
      display.setTextWrap(false);
      display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
      display.setTextSize(2);
      display.setCursor(0, (SCREEN_HEIGHT - 16) / 2);
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

  if (dataValid && (millis() - lastPriceUpdate >= NO_DATA_MS)) {
    dataValid = false;
  }
  bool piDown = (lastPiActivity == 0) || (millis() - lastPiActivity >= NO_DATA_MS);
  if (piDown) {
    rgbRainbow();
    showPiDownScreen();
    delay(100);
    return;
  }
  if (!dataValid) {
    rgbRainbow();
    showNoDataScreen();
    delay(100);
    return;
  }

  rgbSetColor(change24h >= 0 ? 0 : RGB_DIM_LEVEL, change24h >= 0 ? RGB_DIM_LEVEL : 0, 0);
  unsigned long elapsed = millis() - displaySwitchAt;
  if (elapsed >= PRICE_DISPLAY_MS + CHANGE_DISPLAY_MS) {
    displaySwitchAt = millis();
    if (!showingPrice) { showingPrice = true; showPriceScreen(); }
  } else if (elapsed >= PRICE_DISPLAY_MS) {
    if (showingPrice) { showingPrice = false; showChangeScreen(); }
  }
  delay(250);
}
