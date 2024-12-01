#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "Adafruit_SHT31.h"

// WiFi credentials
const char* ssid = "Tbag";
const char* password = "Dbcooper";

// WebSocket server details (we'll update this later with Vercel URL)
const char* websocket_server = "your-app.vercel.app";
const int websocket_port = 443;
const char* websocket_path = "/api/websocket";

Adafruit_SHT31 sht31 = Adafruit_SHT31();
WebSocketsClient webSocket;

unsigned long lastUpdate = 0;
const long interval = 5000;  // Send data every 5 seconds

void setup() {
  Serial.begin(115200);
  Wire.begin(41, 42);  // SDA, SCL

  // Initialize SHT31
  if (!sht31.begin(0x44)) {
    Serial.println("Couldn't find SHT31");
    while (1) delay(1);
  }

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  // Setup WebSocket connection
  webSocket.beginSSL(websocket_server, websocket_port, websocket_path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void loop() {
  webSocket.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - lastUpdate >= interval) {
    lastUpdate = currentMillis;
    sendSensorData();
  }
}

void sendSensorData() {
  float temp = sht31.readTemperature();
  float hum = sht31.readHumidity();
  unsigned long currentTime = millis();

  if (!isnan(temp) && !isnan(hum)) {
    DynamicJsonDocument doc(128);
    doc["temperature"] = temp;
    doc["humidity"] = hum;
    doc["timestamp"] = currentTime;

    String jsonString;
    serializeJson(doc, jsonString);
    webSocket.sendTXT(jsonString);
  }
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("Disconnected from WebSocket server");
      break;
    case WStype_CONNECTED:
      Serial.println("Connected to WebSocket server");
      break;
    case WStype_TEXT:
      Serial.printf("Received text: %s\n", payload);
      break;
  }
} 