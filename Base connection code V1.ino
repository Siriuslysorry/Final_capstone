
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "wifi_config.h"
#include "aws_config.h"
#include "certs.h"

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;

const char* awsEndpoint = AWS_IOT_ENDPOINT;
const char* mqttTopic = AWS_IOT_TOPIC;
const char* thingName = THING_NAME;

WiFiClientSecure net;
PubSubClient mqttClient(net);

String inputBuffer = "";
bool manuallyDisconnected = false;

unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 5000;

// --------------------------------------------------
// Incoming MQTT messages
// --------------------------------------------------

void mqttCallback(char* topic, byte* payload,
                  unsigned int length) {
  Serial.println();
  Serial.println("===== MESSAGE RECEIVED =====");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Message: ");

  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }

  Serial.println();
  Serial.println("============================");
  Serial.print("> ");
}

// --------------------------------------------------
// Wi-Fi
// --------------------------------------------------

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println("Connecting to Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 20000) {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connection failed.");
    return false;
  }

  Serial.println("Wi-Fi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  return true;
}

// --------------------------------------------------
// AWS MQTT connection
// --------------------------------------------------

bool connectAWS() {
  if (!connectWiFi()) {
    return false;
  }

  Serial.println("Connecting to AWS IoT...");

  net.setCACert(ROOT_CA);
  net.setCertificate(DEVICE_CERT);
  net.setPrivateKey(PRIVATE_KEY);

  mqttClient.setServer(awsEndpoint, 8883);
  mqttClient.setCallback(mqttCallback);

  // Keep the MQTT connection alive.
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(15);

  bool connected = mqttClient.connect(thingName);

  if (!connected) {
    Serial.print("MQTT connection failed. State: ");
    Serial.println(mqttClient.state());
    return false;
  }

  Serial.println();
  Serial.println("================================");
  Serial.println("       MQTT CONNECTED!");
  Serial.println("================================");

  // Subscription is optional if you only publish.
  if (mqttClient.subscribe(mqttTopic)) {
    Serial.print("Subscribed to: ");
    Serial.println(mqttTopic);
  } else {
    Serial.println("WARNING: Subscription failed.");
    Serial.println("Check the AWS IoT policy.");
  }

  return true;
}

// --------------------------------------------------
// Process commands typed into Serial Monitor
// --------------------------------------------------

void processCommand(String command) {
  command.trim();

  if (command.length() == 0) {
    return;
  }

  if (command.equalsIgnoreCase("disconnect")) {
    if (mqttClient.connected()) {
      mqttClient.disconnect();
    }

    manuallyDisconnected = true;

    Serial.println();
    Serial.println("MQTT manually disconnected.");
    Serial.println("Type connect to reconnect.");
    Serial.print("> ");
    return;
  }

  if (command.equalsIgnoreCase("connect")) {
    manuallyDisconnected = false;

    if (connectAWS()) {
      Serial.println("Ready to send messages.");
    }

    Serial.print("> ");
    return;
  }

  if (!mqttClient.connected()) {
    Serial.println("Not connected to AWS.");
    Serial.println("Type connect to reconnect.");
    Serial.print("> ");
    return;
  }

  if (mqttClient.publish(mqttTopic, command.c_str())) {
    Serial.println("Message published successfully!");
    Serial.print("Topic: ");
    Serial.println(mqttTopic);
    Serial.print("Payload: ");
    Serial.println(command);
  } else {
    Serial.println("Publish failed.");
    Serial.print("MQTT state: ");
    Serial.println(mqttClient.state());
  }

  Serial.print("> ");
}

// --------------------------------------------------
// Setup
// --------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("ESP32 AWS IoT Interactive Terminal");

  if (connectWiFi()) {
    connectAWS();
  }

  Serial.println();
  Serial.println("Commands:");
  Serial.println("  Type a message and press Enter to publish");
  Serial.println("  disconnect  - disconnect from MQTT");
  Serial.println("  connect     - reconnect to MQTT");
  Serial.println();

  Serial.print("> ");
}

// --------------------------------------------------
// Main loop
// --------------------------------------------------

void loop() {
  // Maintain MQTT processing and keep-alive traffic.
  if (mqttClient.connected()) {
    mqttClient.loop();
  } else if (!manuallyDisconnected) {
    // Retry at intervals rather than continuously.
    if (millis() - lastReconnectAttempt >=
        RECONNECT_INTERVAL) {
      lastReconnectAttempt = millis();

      Serial.println();
      Serial.println("MQTT disconnected. Reconnecting...");

      connectAWS();
      Serial.print("> ");
    }
  }

  // Read input without blocking MQTT processing.
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        String command = inputBuffer;
        inputBuffer = "";
        processCommand(command);
      }
    } else {
      inputBuffer += c;
    }
  }

  delay(2);
}
