#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"

// ========================================
// 1. Wi-Fi Configuration
// ========================================
const char* ssid = "Youngtang";
const char* password = "12345678";

// ========================================
// 2. MQTT Broker Configuration
// ========================================
const char* mqtt_server = "172.20.10.3";
const int mqtt_port = 1883;
const char* mqtt_client_id = "ESP32_DHT11_Client";

// ========================================
// 3. GPIO Configuration
// ========================================
#define DHTPIN 19
#define DHTTYPE DHT11

#define LED_PIN 18
#define BUTTON_PIN 4

// ========================================
// 4. MQTT Topics
// ========================================
const char* topic_temp = "home/room/temperature";
const char* topic_humidity = "home/room/humidity";
const char* topic_status = "home/room/status";
const char* topic_cmd = "home/room/set_status";
const char* topic_availability = "home/room/availability";

// ========================================
// 5. Initialize Objects
// ========================================
DHT dht(DHTPIN, DHTTYPE);

WiFiClient espClient;
PubSubClient client(espClient);

// ========================================
// 6. System Variables
// ========================================
bool systemActive = false;
bool wifiWasConnected = false;

// Button Debounce
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Sensor Timer
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 2000;

// MQTT Reconnect Timer
unsigned long lastMQTTAttempt = 0;
const unsigned long mqttRetryInterval = 5000;

// ========================================
// 7. Update System State
// ========================================
void updateSystemState() {

  // Control LED
  digitalWrite(LED_PIN, systemActive ? HIGH : LOW);

  Serial.print("System Status: ");
  Serial.println(systemActive ? "ON" : "OFF");

  // Publish Status
  if (client.connected()) {
    client.publish(
      topic_status,
      systemActive ? "ON" : "OFF",
      true
    );
  }
}

// ========================================
// 8. MQTT Callback
// ========================================
void callback(char* topic, byte* payload, unsigned int length) {

  String message = "";

  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  message.trim();

  Serial.print("MQTT Received: ");
  Serial.println(message);

  if (String(topic) == topic_cmd) {

    if (message == "ON") {
      systemActive = true;
      updateSystemState();
    }

    else if (message == "OFF") {
      systemActive = false;
      updateSystemState();
    }
  }
}

// ========================================
// 9. Wi-Fi Connection
// ========================================
void setupWiFi() {

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  WiFi.begin(ssid, password);

  Serial.println("Connecting to Wi-Fi...");
}

// ========================================
// 10. MQTT Reconnection
// ========================================
void reconnectMQTT() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (client.connected()) {
    return;
  }

  if (millis() - lastMQTTAttempt < mqttRetryInterval) {
    return;
  }

  lastMQTTAttempt = millis();

  Serial.println("Connecting to MQTT...");

  if (client.connect(
        mqtt_client_id,
        topic_availability,
        0,
        true,
        "offline"
      )) {

    Serial.println("MQTT Connected!");

    // Subscribe to control commands
    client.subscribe(topic_cmd);

    // Publish online status
    client.publish(
      topic_availability,
      "online",
      true
    );

    // Publish current system status
    updateSystemState();

  } else {

    Serial.print("MQTT Failed, rc=");
    Serial.println(client.state());
  }
}

// ========================================
// 11. Push Button Control
// ========================================
void checkButton() {

  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) >= debounceDelay) {

    if (reading != stableButtonState) {

      stableButtonState = reading;

      if (stableButtonState == LOW) {

        systemActive = !systemActive;

        Serial.println("Push Button Pressed");

        updateSystemState();

        // Start measurement on next loop when ON
        if (systemActive) {
          lastSendTime = millis() - sendInterval;
        }
      }
    }
  }

  lastButtonReading = reading;
}

// ========================================
// 12. Temperature & Humidity Measurement
// ========================================
void readSensor() {

  if (!systemActive) {
    return;
  }

  if (millis() - lastSendTime < sendInterval) {
    return;
  }

  lastSendTime = millis();

  // Read DHT11
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  // Check sensor reading
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Failed to read DHT11!");
    return;
  }

  // Check valid sensor range
  if (temperature < 0 || temperature > 50 ||
      humidity < 20 || humidity > 90) {

    Serial.println("Invalid DHT11 Data!");
    return;
  }

  // Display temperature
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" °C");

  // Display humidity
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  // MQTT Publish
  if (client.connected()) {

    String tempPayload = String(temperature, 2);
    String humPayload = String(humidity, 2);

    bool tempPublished = client.publish(
      topic_temp,
      tempPayload.c_str()
    );

    bool humPublished = client.publish(
      topic_humidity,
      humPayload.c_str()
    );

    if (tempPublished && humPublished) {
      Serial.println("MQTT Sensor Data Published!");
    } else {
      Serial.println("MQTT Publish Failed!");
    }

  } else {
    Serial.println("MQTT Not Connected!");
  }

  Serial.println("----------------------------");
}

// ========================================
// 13. Setup
// ========================================
void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.println("============================");
  Serial.println("ESP32 DHT11 MQTT System");
  Serial.println("============================");

  // GPIO Setup
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(LED_PIN, LOW);

  // Initialize DHT11
  dht.begin();

  // Initialize Wi-Fi
  setupWiFi();

  // Initialize MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  Serial.println("System Ready");
  Serial.println("Initial Status: OFF");
}

// ========================================
// 14. Main Loop
// ========================================
void loop() {

  // =====================================
  // 1. Wi-Fi Connection
  // =====================================
  if (WiFi.status() == WL_CONNECTED) {

    if (!wifiWasConnected) {

      wifiWasConnected = true;

      Serial.println();
      Serial.println("============================");
      Serial.println("Wi-Fi Connected!");

      Serial.print("ESP32 IP: ");
      Serial.println(WiFi.localIP());

      Serial.print("Gateway: ");
      Serial.println(WiFi.gatewayIP());

      Serial.print("MQTT Broker: ");
      Serial.println(mqtt_server);

      Serial.println("============================");
    }

    // =====================================
    // 2. MQTT Connection
    // =====================================
    if (!client.connected()) {
      reconnectMQTT();
    }

    if (client.connected()) {
      client.loop();
    }

  } else {

    if (wifiWasConnected) {
      wifiWasConnected = false;
      Serial.println("Wi-Fi Disconnected!");
    }
  }

  // =====================================
  // 3. Push Button
  // =====================================
  checkButton();

  // =====================================
  // 4. DHT11 Sensor
  // =====================================
  readSensor();
}