/*
 * ESP8266 MQTT IR Transmitter
 *
 * Subscribes to MQTT topic and transmits received IR codes.
 * Topic: hydration/commands/ir_transmit
 * Payload: Hex Code (e.g., "0xF7F00F") -> Transmits NEC protocol by default
 */

#include <ESP8266WiFi.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <PubSubClient.h>

// ==================== Configuration ====================
const char *wifi_ssid = "No 303";
const char *wifi_password = "3.14159265";
const char *mqtt_server = "raspberrypi.local";
const int mqtt_port = 1883;

// Topics
const char *topic_ir_transmit = "hydration/commands/ir_transmit";
const char *topic_relay_control = "hydration/commands/relay_control";

// IR Pin
const uint16_t IR_SEND_PIN = 4; // D2

// Relay Pins
const uint8_t RELAY_PINS[4] = {5, 14, 12, 13}; // D1, D5, D6, D7
// D1=GPIO5, D5=GPIO14, D6=GPIO12, D7=GPIO13

// Objects
WiFiClient espClient;
PubSubClient client(espClient);
IRsend irSender(IR_SEND_PIN);

// Global
long lastReconnectAttempt = 0;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);

  if (strcmp(topic, topic_ir_transmit) == 0) {
    // Parse Hex String: "0xF7F00F"
    uint64_t code = strtoull(message, NULL, 16);

    if (code > 0) {
      Serial.print("Transmitting NEC: 0x");
      Serial.println((unsigned long)code, HEX); // Cast for print safety

      // Send NEC code (32 bits is standard)
      irSender.sendNEC(code, 32);
      Serial.println("Done.");
    } else {
      Serial.println("Invalid code received.");
    }
  } else if (strcmp(topic, topic_relay_control) == 0) {
    // Payload format: "1:ON" or "2:OFF"
    // Parse Relay ID
    if (length >= 4) {
      char cId = message[0];
      int relayId = cId - '0'; // '1' -> 1

      if (relayId >= 1 && relayId <= 4) {
        int pinIdx = relayId - 1;

        // Parse State (Check for 'O' 'N' or 'O' 'F' 'F')
        // message[2] is start of state
        if (strstr(message, "ON") != NULL) {
          digitalWrite(RELAY_PINS[pinIdx], LOW); // Active LOW -> ON
          Serial.print("Relay ");
          Serial.print(relayId);
          Serial.println(" ON");
        } else if (strstr(message, "OFF") != NULL) {
          digitalWrite(RELAY_PINS[pinIdx], HIGH); // Active LOW -> OFF
          Serial.print("Relay ");
          Serial.print(relayId);
          Serial.println(" OFF");
        }
      }
    }
  }
}

boolean reconnect() {
  if (client.connect("ESP8266_IR_Transmitter")) {
    Serial.println("Connected to MQTT broker");
    client.subscribe(topic_ir_transmit);
    client.subscribe(topic_relay_control);
    Serial.println("Subscribed to hydration/commands/...");
  }
  return client.connected();
}

void setup() {
  Serial.begin(9600);

  irSender.begin();

  // Initialize Relays (Active LOW is common for modules, but we start HIGH=OFF
  // usually? actually most modules are Active LOW (LOW=ON). Let's assume Active
  // LOW: OFF = HIGH. User asked for "on/off", I will map "ON" -> LOW, "OFF" ->
  // HIGH.
  for (int i = 0; i < 4; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], HIGH); // Default OFF if active low
  }

  // BOOT IR Signal
  Serial.println("Sending Boot IR Signal...");
  irSender.sendNEC(0xF7F00F, 32);
  Serial.println("Boot Signal Sent.");

  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    client.loop();
  }
}
