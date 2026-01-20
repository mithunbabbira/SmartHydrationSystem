/**
 * ESP32 BLE LED Controller for HealthyLighting / Triones LED Strips
 * with MQTT Control and State Persistence
 */

#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>

// ==================== Configuration ====================
// Wi-Fi
const char *wifi_ssid = "No 303";
const char *wifi_password = "3.14159265";

// MQTT
const char *mqtt_server = "raspberrypi.local";
const int mqtt_port = 1883;
const char *mqtt_client_id = "ESP32_LED_Controller";
const char *MQTT_USER = "babbira";
const char *MQTT_PASSWORD = "3.14159265";

// MQTT Topics
const char *topic_commands_root = "hydration/commands/led_strip/";
const char *topic_status = "hydration/status/led_strip";
const char *topic_presence = "hydration/status/led_strip/presence";

// Target BLE Device Address
// Replace with your LED strip's MAC address if known, or use scanning logic
static String targetAddress = "ff:ff:bc:09:a5:b9";

// Standard Triones UUIDs
static BLEUUID serviceUUID("FFD5");
static BLEUUID charUUID("FFD9");

// ==================== Globals ====================
WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;

// BLE State
bool connected = false;
BLERemoteCharacteristic *pRemoteCharacteristic;
BLEClient *pClient = nullptr;
bool doConnect = false;

// Controller State
struct LedState {
  bool isOn;
  uint8_t mode;  // 0 = Solid Color, >0 = Pattern ID
  uint8_t speed; // 1-100
  uint8_t r, g, b;
  uint8_t brightness; // 0-100 (Software scaling if hardware doesn't support
                      // generic brightness)
};

LedState currentState;

// Protocol Constants
const byte HEADER_COLOR = 0x56;
const byte HEADER_POWER = 0xCC;
const byte HEADER_MODE =
    0xBB; // 0xBB is common for mode, but python uses 256-69 = 187 = 0xBB?
// wait, python says: 256 - 69 = 187. 187 is 0xBB. Correct.

// ==================== Function Prototypes ====================
void setupWifi();
void reconnectMqtt();
void saveState();
void loadState();
void applyState();
bool connectToDevice();
void sendColor(uint8_t r, uint8_t g, uint8_t b);
void sendPower(bool on);
void sendMode(uint8_t mode, uint8_t speed);

// ==================== Implementation ====================

// --- Persistence ---
void loadState() {
  preferences.begin("led-state", true); // Read-only
  currentState.isOn = preferences.getBool("on", true);
  currentState.mode = preferences.getUChar("mode", 0); // 0 = Static
  currentState.speed = preferences.getUChar("speed", 50);
  currentState.r = preferences.getUChar("r", 255);
  currentState.g = preferences.getUChar("g", 255);
  currentState.b = preferences.getUChar("b", 255);
  currentState.brightness = preferences.getUChar("bri", 100);
  preferences.end();
  Serial.println("State Loaded");
}

void saveState() {
  preferences.begin("led-state", false); // Read-write
  preferences.putBool("on", currentState.isOn);
  preferences.putUChar("mode", currentState.mode);
  preferences.putUChar("speed", currentState.speed);
  preferences.putUChar("r", currentState.r);
  preferences.putUChar("g", currentState.g);
  preferences.putUChar("b", currentState.b);
  preferences.putUChar("bri", currentState.brightness);
  preferences.end();
  Serial.println("State Saved");
}

// --- BLE Protocol ---
// Python Ref: lista = [86, R, G, B, (int(10 * 255 / 100) & 0xFF), 256-16,
// 256-86] 256-16 = 240 = 0xF0 256-86 = 170 = 0xAA 86 = 0x56 (HEADER_COLOR)
void sendColor(uint8_t r, uint8_t g, uint8_t b) {
  if (!connected || pRemoteCharacteristic == nullptr)
    return;

  // Scale by brightness if needed (Triones usually full brightness on color
  // command) Optional: float scale = currentState.brightness / 100.0; uint8_t
  // sr = r * scale; ...

  uint8_t command[] = {HEADER_COLOR, r, g, b, 0x00, 0xF0, 0xAA};
  pRemoteCharacteristic->writeValue(command, sizeof(command));
  Serial.printf("BLE: Color R%d G%d B%d\n", r, g, b);
}

// Python Ref: [204, 35, 51] for ON, [204, 36, 51] for OFF
// 204 = 0xCC (HEADER_POWER)
// 35 = 0x23 (ON)
// 36 = 0x24 (OFF)
// 51 = 0x33 (TAIL)
void sendPower(bool on) {
  if (!connected || pRemoteCharacteristic == nullptr)
    return;

  uint8_t stateByte = on ? 0x23 : 0x24;
  uint8_t command[] = {HEADER_POWER, stateByte, 0x33};
  pRemoteCharacteristic->writeValue(command, sizeof(command));
  Serial.printf("BLE: Power %s\n", on ? "ON" : "OFF");
}

// Python Ref: [256 - 69, mode, (byte)(speed & 0xFF), 68]
// 256-69 = 187 = 0xBB
// 68 = 0x44
void sendMode(uint8_t mode, uint8_t speed) {
  if (!connected || pRemoteCharacteristic == nullptr)
    return;

  uint8_t command[] = {0xBB, mode, speed, 0x44};
  pRemoteCharacteristic->writeValue(command, sizeof(command));
  Serial.printf("BLE: Mode %d Speed %d\n", mode, speed);
}

void applyState() {
  if (!connected)
    return;

  // Always send power first
  sendPower(currentState.isOn);
  delay(50); // Small delay to ensure processing

  if (currentState.isOn) {
    if (currentState.mode == 0) {
      sendColor(currentState.r, currentState.g, currentState.b);
    } else {
      sendMode(currentState.mode, currentState.speed);
    }
  }
}

// --- MQTT Callback ---
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++)
    msg += (char)payload[i];

  Serial.printf("MQTT Rx: %s -> %s\n", topic, msg.c_str());

  String cmdTopic = String(topic);
  bool stateChanged = false;

  // Power Control
  if (cmdTopic.endsWith("power")) {
    // "ON", "OFF" or "1", "0"
    msg.toUpperCase();
    bool newState = (msg == "ON" || msg == "1" || msg == "TRUE");
    if (currentState.isOn != newState) {
      currentState.isOn = newState;
      stateChanged = true;
      // Immediate effect logic
      if (connected)
        sendPower(newState);
      // If turning ON, we might need to resend color/mode after power
      if (newState && connected) {
        delay(100);
        if (currentState.mode == 0)
          sendColor(currentState.r, currentState.g, currentState.b);
        else
          sendMode(currentState.mode, currentState.speed);
      }
    }
  }
  // Color Control
  else if (cmdTopic.endsWith("color")) {
    // Hex "RRGGBB" or "RRGGBB"
    if (msg.startsWith("#"))
      msg = msg.substring(1);
    if (msg.length() == 6) {
      long rgb = strtol(msg.c_str(), NULL, 16);
      currentState.r = (rgb >> 16) & 0xFF;
      currentState.g = (rgb >> 8) & 0xFF;
      currentState.b = rgb & 0xFF;
      currentState.mode = 0; // Switch to static color mode
      stateChanged = true;
      if (currentState.isOn)
        sendColor(currentState.r, currentState.g, currentState.b);
    }
  }
  // Mode Control
  else if (cmdTopic.endsWith("mode")) {
    // "37" (Rainbow), "38" (Pulse), etc.
    // Or "static" to go back to color
    if (msg.equalsIgnoreCase("static")) {
      currentState.mode = 0;
      stateChanged = true;
      if (currentState.isOn)
        sendColor(currentState.r, currentState.g, currentState.b);
    } else {
      int mode = msg.toInt();
      if (mode > 0 && mode < 255) {
        currentState.mode = mode;
        stateChanged = true;
        if (currentState.isOn)
          sendMode(currentState.mode, currentState.speed);
      }
    }
  }
  // Speed Control
  else if (cmdTopic.endsWith("speed")) {
    int spd = msg.toInt();
    if (spd > 0 && spd <= 100) {
      currentState.speed = spd;
      stateChanged = true;
      if (currentState.isOn && currentState.mode != 0) {
        sendMode(currentState.mode, currentState.speed);
      }
    }
  }
  // Brightness Control
  else if (cmdTopic.endsWith("brightness")) {
    int bri = msg.toInt();
    if (bri >= 0 && bri <= 100) {
      currentState.brightness = bri;
      stateChanged = true;
      // If static color, re-send (if we implement scaling)
      // For now, just save it
    }
  }

  if (stateChanged) {
    saveState();
    // Feedback
    // client.publish(topic_status, "updated");
  }
}

// --- BLE Connection Logic ---
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) {
    connected = true;
    Serial.println(">>> BLE Connected");
    client.publish(topic_presence, "connected");
    // Apply state upon connection
    // We do this in the main loop to handle delays safely
  }

  void onDisconnect(BLEClient *pclient) {
    connected = false;
    Serial.println(">>> BLE Disconnected");
    client.publish(topic_presence, "disconnected");
  }
};

bool connectToDevice() {
  Serial.print("Connecting to BLE: ");
  Serial.println(targetAddress);

  BLEAddress addr(targetAddress.c_str());

  // Connect to the remote BLE Server.
  if (!pClient->connect(addr)) {
    Serial.println("Failed to connect BLE");
    return false;
  }

  // Obtain reference to the service
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.println("Failed to find service FFD5");
    pClient->disconnect();
    return false;
  }

  // Obtain reference to the characteristic
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.println("Failed to find characteristic FFD9");
    pClient->disconnect();
    return false;
  }

  return true;
}

// --- Wi-Fi & MQTT ---
void setupWifi() {
  delay(10);
  Serial.print("\nConnecting to WiFi: ");
  Serial.println(wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println("IP: " + WiFi.localIP().toString());
}

void reconnectMqtt() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(mqtt_client_id, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("connected");

      // Subscribe to all command subtopics
      String sub = String(topic_commands_root) + "#";
      client.subscribe(sub.c_str());
      Serial.println("Subscribed to: " + sub);

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// ==================== Main ====================
void setup() {
  Serial.begin(115200);
  Serial.println("Booting BLE LED Controller...");

  // Loading State
  loadState();

  // WiFi & MQTT
  setupWifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

  // BLE Init
  BLEDevice::init("");
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
}

long lastBleCheck = 0;
bool stateAppliedAfterConnect = false;

void loop() {
  // 1. Maintain WiFi/MQTT
  if (!client.connected()) {
    reconnectMqtt();
  }
  client.loop();

  // 2. Maintain BLE Connection
  long now = millis();
  if (!connected) {
    if (now - lastBleCheck > 5000) { // Retry every 5s
      lastBleCheck = now;
      if (connectToDevice()) {
        stateAppliedAfterConnect = false; // Trigger state sync
      }
    }
  } else {
    // We are connected
    // Sync state once after connection established
    if (!stateAppliedAfterConnect) {
      Serial.println("Synchronizing state to LED strip...");
      delay(500); // Give connection a moment to settle
      applyState();
      stateAppliedAfterConnect = true;
    }
  }

  delay(10); // Small yield
}
