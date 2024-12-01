#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>

const char* ssid = "Tbag";
const char* password = "Dbcooper";

ESP8266WebServer server(80);
SoftwareSerial arduinoSerial(D1, D2); // RX, TX

struct SensorData {
  float temperature;
  float humidity;
  float vpd;
  float pH;
  float waterLevel;
  float reservoirVolume;
  int lightIntensity;
} sensorData;

void setup() {
  Serial.begin(115200);
  arduinoSerial.begin(9600);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/control", handleControl);
  server.begin();
}

void loop() {
  server.handleClient();
  if (arduinoSerial.available()) {
    String jsonStr = arduinoSerial.readStringUntil('\n');
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }
    sensorData.temperature = doc["temperature"];
    sensorData.humidity = doc["humidity"];
    sensorData.vpd = doc["vpd"];
    sensorData.pH = doc["pH"];
    sensorData.waterLevel = doc["waterLevel"];
    sensorData.reservoirVolume = doc["reservoirVolume"];
    sensorData.lightIntensity = doc["lightIntensity"];
  }
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Aeroponic Control Panel</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background: linear-gradient(120deg, #84fab0 0%, #8fd3f4 100%);
            margin: 0;
            padding: 20px;
            color: #333;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
            background-color: rgba(255, 255, 255, 0.9);
            border-radius: 10px;
            padding: 20px;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        h1 {
            color: #2c3e50;
            text-align: center;
        }
        .sensor-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            margin-bottom: 20px;
        }
        .sensor-card {
            background-color: #fff;
            border-radius: 5px;
            padding: 15px;
            text-align: center;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        .sensor-value {
            font-size: 24px;
            font-weight: bold;
            margin: 10px 0;
        }
        .controls {
            display: flex;
            flex-wrap: wrap;
            justify-content: space-around;
        }
        .control-item {
            margin: 10px;
        }
        input[type="range"] {
            width: 200px;
        }
        button {
            background-color: #3498db;
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 5px;
            cursor: pointer;
            transition: background-color 0.3s;
        }
        button:hover {
            background-color: #2980b9;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Aeroponic Control Panel</h1>
        <div class="sensor-grid" id="sensorGrid"></div>
        <div class="controls">
            <div class="control-item">
                <label for="lightThreshold">Light Threshold:</label>
                <input type="range" id="lightThreshold" min="0" max="1023" value="300">
                <span id="lightThresholdValue">300</span>
            </div>
            <div class="control-item">
                <label for="pHTarget">pH Target:</label>
                <input type="range" id="pHTarget" min="5.5" max="6.5" step="0.1" value="6.0">
                <span id="pHTargetValue">6.0</span>
            </div>
            <div class="control-item">
                <button onclick="manualPump('vpd')">VPD Pump</button>
                <button onclick="manualPump('acid')">Acid Pump</button>
                <button onclick="manualPump('base')">Base Pump</button>
            </div>
        </div>
    </div>
    <script>
        function updateSensorData() {
            fetch('/data')
                .then(response => response.json())
                .then(data => {
                    const sensorGrid = document.getElementById('sensorGrid');
                    sensorGrid.innerHTML = '';
                    for (const [key, value] of Object.entries(data)) {
                        const card = document.createElement('div');
                        card.className = 'sensor-card';
                        card.innerHTML = `
                            <h3>${key.replace(/([A-Z])/g, ' $1').trim()}</h3>
                            <div class="sensor-value">${value}</div>
                        `;
                        sensorGrid.appendChild(card);
                    }
                });
        }

        function updateControl(control, value) {
            fetch('/control', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({ [control]: value }),
            });
        }

        function manualPump(pump) {
            fetch('/control', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({ manualPump: pump }),
            });
        }

        document.getElementById('lightThreshold').addEventListener('input', function() {
            document.getElementById('lightThresholdValue').textContent = this.value;
            updateControl('lightThreshold', this.value);
        });

        document.getElementById('pHTarget').addEventListener('input', function() {
            document.getElementById('pHTargetValue').textContent = this.value;
            updateControl('pHTarget', this.value);
        });

        setInterval(updateSensorData, 5000);
        updateSensorData();
    </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleData() {
  DynamicJsonDocument doc(1024);
  doc["Temperature"] = String(sensorData.temperature, 1) + " °C";
  doc["Humidity"] = String(sensorData.humidity, 1) + " %";
  doc["VPD"] = String(sensorData.vpd, 2) + " kPa";
  doc["pH"] = String(sensorData.pH, 2);
  doc["WaterLevel"] = String(sensorData.waterLevel, 1) + " cm";
  doc["ReservoirVolume"] = String(sensorData.reservoirVolume, 1) + " L";
  doc["LightIntensity"] = String(sensorData.lightIntensity);

  String jsonString;
  serializeJson(doc, jsonString);
  server.send(200, "application/json", jsonString);
}

void handleControl() {
  if (server.hasArg("plain")) {
    String message;
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, server.arg("plain"));
    
    if (doc.containsKey("lightThreshold")) {
      int lightThreshold = doc["lightThreshold"];
      message = "Light threshold set to: " + String(lightThreshold);
      arduinoSerial.println("LT:" + String(lightThreshold));
    } else if (doc.containsKey("pHTarget")) {
      float pHTarget = doc["pHTarget"];
      message = "pH target set to: " + String(pHTarget);
      arduinoSerial.println("PT:" + String(pHTarget));
    } else if (doc.containsKey("manualPump")) {
      String pump = doc["manualPump"];
      message = "Manual pump activated: " + pump;
      arduinoSerial.println("MP:" + pump);
    }
    
    server.send(200, "text/plain", message);
  }
}
