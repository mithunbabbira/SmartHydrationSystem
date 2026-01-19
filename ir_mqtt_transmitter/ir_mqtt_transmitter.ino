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

// IR Pin
const uint16_t IR_SEND_PIN = 4; // D2

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
  }
}

boolean reconnect() {
  if (client.connect("ESP8266_IR_Transmitter")) {
    Serial.println("Connected to MQTT broker");
    client.subscribe(topic_ir_transmit);
    Serial.println("Subscribed to: hydration/commands/ir_transmit");
  }
  return client.connected();
}

void setup() {
  Serial.begin(9600);

  irSender.begin();

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
