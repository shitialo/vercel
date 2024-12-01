#include <Wire.h>
#include <math.h>
#include <AccelStepper.h>
#include "Adafruit_SHT31.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// Pin Definitions for ESP32-S3
#define PH_PIN 1          // ADC1_CH0
#define VPD_PUMP_RELAY 2  
#define ACID_PUMP_RELAY 3 
#define BASE_PUMP_RELAY 4 
#define MIX_PUMP_RELAY 5  
#define TRIG_PIN 6        
#define ECHO_PIN 7        
#define LDR_PIN 2        // Changed to ADC1_CH1 for analog reading
#define STEPPER_STEP_PIN 9 
#define STEPPER_DIR_PIN 10  

// Constants (unchanged)
#define VPD_PUMP_DURATION 5000
#define MIX_PUMP_DURATION 1000
#define PH_CHECK_INTERVAL 30000
#define PH_WAIT_INTERVAL 18000
#define PH_LOWER_LIMIT 5.5
#define PH_UPPER_LIMIT 6.5
#define DOSAGE_RATE 0.00025
#define RESERVOIR_RADIUS 20.0
#define RESERVOIR_HEIGHT 35.0
#define RESERVOIR_CHECK_INTERVAL 3600
#define ROTATION_INTERVAL 5000
#define STEPS_PER_REVOLUTION 200
#define STEPS_90_DEGREES (STEPS_PER_REVOLUTION / 4)

// Global variables
Adafruit_SHT31 sht31 = Adafruit_SHT31();
AccelStepper stepper(AccelStepper::DRIVER, STEPPER_STEP_PIN, STEPPER_DIR_PIN);

unsigned long lastVPDCycleTime = 0;
unsigned long vpdCycleInterval = 1200;
unsigned long lastpHCheckTime = 0;
unsigned long lastReservoirCheckTime = 0;
unsigned long lastRotationTime = 0;

bool isVPDPumping = false;
bool isPHAdjusting = false;
bool isPHWaiting = false;

long ph_pump_duration = 0;

float temperature = 0.0;
float humidity = 0.0;
float vpd = 0.0;
float pH = 0.0;
float waterLevel = 0.0;
float reservoirVolume = 0.0;
int lightIntensity = 0;  // Changed from bool to int
float PH_TARGET = 6.0;

// Replace the existing WiFi credentials with AP settings
const char* ap_ssid = "Aeroponics_Control";     // Name of the WiFi network to create
const char* ap_password = "aero1234";           // Password for the WiFi network
IPAddress local_ip(192,168,1,1);               // IP address for the ESP32
IPAddress gateway(192,168,1,1);                // Gateway (same as IP for AP mode)
IPAddress subnet(255,255,255,0);               // Subnet mask

// Create WebServer object after your existing global variables
WebServer server(80);

int LIGHT_THRESHOLD = 2000;  // Initial value, can be modified at runtime

// Add these at the top with other global variables
unsigned long lastDataUpdate = 0;
const unsigned long DATA_UPDATE_INTERVAL = 1000; // Update data every second

// Add these default values with the other global variables at the top
const float DEFAULT_TEMPERATURE = 25.0;
const float DEFAULT_HUMIDITY = 60.0;

// Add these global variables for tracking states
bool isMistingActive = false;
bool isRotating = false;
String phStatus = "stable"; // Can be "stable", "adjusting", or "completed"

// Function declarations
void handleRoot();
void handleData();
void handleControl();
void checkNewClients();
void handleVPDControl(unsigned long currentTime);
void handlePHControl(unsigned long currentTime);
void checkReservoirVolume(unsigned long currentTime);
void checkLightAndRotate(unsigned long currentTime);
void checkAndAdjustPH(unsigned long currentTime);
float readpH();
float calculateVPD(float temperature, float humidity);
void updateVPDCycleInterval(float vpd);
float measureWaterLevel();
float calculateReservoirVolume(float waterLevel);

void setup() {
  Serial.begin(115200);
  Wire.begin(41, 42);  // ESP32-S3 default I2C pins: SDA=41, SCL=42
  
  pinMode(VPD_PUMP_RELAY, OUTPUT);
  pinMode(ACID_PUMP_RELAY, OUTPUT);
  pinMode(BASE_PUMP_RELAY, OUTPUT);
  pinMode(MIX_PUMP_RELAY, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  // Remove this line since analog pins don't need pinMode
  // pinMode(LDR_PIN, INPUT);  
  
  digitalWrite(VPD_PUMP_RELAY, HIGH);
  digitalWrite(ACID_PUMP_RELAY, HIGH);
  digitalWrite(BASE_PUMP_RELAY, HIGH);
  digitalWrite(MIX_PUMP_RELAY, HIGH);
  
  if (!sht31.begin(0x44)) {
    Serial.println("Warning: Couldn't find SHT31 sensor. Will continue with default values.");
  }
  
  stepper.setMaxSpeed(1000);
  stepper.setAcceleration(500);

  // ESP32 ADC setup
  analogReadResolution(12); // ESP32 has 12-bit ADC

  // Replace the existing WiFi setup with this:
  WiFi.mode(WIFI_AP);                          // Set ESP32 as an Access Point
  WiFi.softAPConfig(local_ip, gateway, subnet); // Configure the AP
  WiFi.softAP(ap_ssid, ap_password);           // Start the AP

  Serial.println("Access Point Started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());             // Print the IP address
  Serial.print("Network Name: ");
  Serial.println(ap_ssid);
  Serial.print("Password: ");
  Serial.println(ap_password);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/control", handleControl);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  unsigned long currentTime = millis();
  
  server.handleClient();
  checkNewClients();  // Add this line to monitor connections

  // Read sensor data
  temperature = sht31.readTemperature();
  humidity = sht31.readHumidity();
  vpd = calculateVPD(temperature, humidity);
  pH = readpH();
  waterLevel = measureWaterLevel();
  reservoirVolume = calculateReservoirVolume(waterLevel);
  lightIntensity = analogRead(LDR_PIN);  // Changed to analogRead

  handleVPDControl(currentTime);
  handlePHControl(currentTime);
  checkReservoirVolume(currentTime);
  checkLightAndRotate(currentTime);
  
  stepper.run();
}

// Modified pH reading for ESP32's 12-bit ADC
float readpH() {
  int sensorValue = analogRead(PH_PIN);
  // ESP32 ADC is 12-bit (0-4095)
  return map(sensorValue, 0, 4095, 0, 14);
}

// The rest of the functions remain the same as they don't need ESP-specific modifications
// Just removing yield() calls as they're not needed for ESP32

void checkLightAndRotate(unsigned long currentTime) {
  if (currentTime - lastRotationTime >= ROTATION_INTERVAL) {
    lastRotationTime = currentTime;
    
    int lightLevel = analogRead(LDR_PIN);
    Serial.printf("Light intensity: %d\n", lightLevel);

    if (lightLevel > LIGHT_THRESHOLD) {
      isRotating = true;
      stepper.moveTo(stepper.currentPosition() + STEPS_90_DEGREES);
      while (stepper.distanceToGo() != 0) {
        stepper.run();
      }
      isRotating = false;
    } else {
      Serial.println("Insufficient light, not rotating");
    }
  }
}

// Include all other functions here (handleVPDControl, handlePHControl, etc.)
// They remain the same as in your original code, just remove the yield() calls 

void handleVPDControl(unsigned long currentTime) {
  if (currentTime - lastVPDCycleTime >= vpdCycleInterval) {
    lastVPDCycleTime = currentTime;
    
    float humidity = sht31.readHumidity();
    float temperature = sht31.readTemperature();

    // Use default values if readings are invalid
    if (isnan(humidity) || isnan(temperature)) {
      humidity = DEFAULT_HUMIDITY;
      temperature = DEFAULT_TEMPERATURE;
      Serial.println("Warning: Using default temperature and humidity values");
    }

    float vpd = calculateVPD(temperature, humidity);
    updateVPDCycleInterval(vpd);
    
    Serial.printf("Humidity: %.1f%%, Temperature: %.1f°C, VPD: %.2f kPa\n", 
                 humidity, temperature, vpd);

    digitalWrite(VPD_PUMP_RELAY, LOW);
    isVPDPumping = true;
    Serial.println("VPD Pump activated");
  }

  if (isVPDPumping && currentTime - lastVPDCycleTime >= VPD_PUMP_DURATION) {
    digitalWrite(VPD_PUMP_RELAY, HIGH);
    isVPDPumping = false;
    Serial.println("VPD Pump deactivated");
  }
}

void handlePHControl(unsigned long currentTime) {
  if (!isPHAdjusting && !isPHWaiting && currentTime - lastpHCheckTime >= PH_CHECK_INTERVAL) {
    phStatus = "stable";
    checkAndAdjustPH(currentTime);
  }

  if (isPHWaiting && currentTime - lastpHCheckTime >= PH_WAIT_INTERVAL) {
    isPHWaiting = false;
    checkAndAdjustPH(currentTime);
  }

  if (isPHAdjusting && currentTime - lastpHCheckTime >= ph_pump_duration) {
    digitalWrite(ACID_PUMP_RELAY, HIGH);
    digitalWrite(BASE_PUMP_RELAY, HIGH);
    digitalWrite(MIX_PUMP_RELAY, LOW);
    
    delay(MIX_PUMP_DURATION);
    
    digitalWrite(MIX_PUMP_RELAY, HIGH);
    isPHAdjusting = false;
    isPHWaiting = true;
    Serial.println("pH adjustment cycle completed, waiting before rechecking");
  }
}

void checkReservoirVolume(unsigned long currentTime) {
  if (currentTime - lastReservoirCheckTime >= RESERVOIR_CHECK_INTERVAL) {
    lastReservoirCheckTime = currentTime;
    
    float waterLevel = measureWaterLevel();
    float volume = calculateReservoirVolume(waterLevel);
    
    Serial.printf("Volume: %.1f liters\n", volume);
    ph_pump_duration = volume * DOSAGE_RATE * 1000000; // Convert to ms
  }
}

float calculateVPD(float temperature, float humidity) {
  float svp = 0.6108 * exp(17.27 * temperature / (temperature + 237.3)); 
  float avp = (humidity / 100.0) * svp;
  return svp - avp;
}

void updateVPDCycleInterval(float vpd) {
  vpdCycleInterval = (vpd > 1.5) ? 6000 : (vpd < 0.8) ? 18000 : 12000;
  Serial.printf("New VPD cycle interval: %d seconds\n", vpdCycleInterval / 1000);
}

float measureWaterLevel() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH);
  return RESERVOIR_HEIGHT - (duration * 0.034 / 2);
}

float calculateReservoirVolume(float waterLevel) {
  return PI * RESERVOIR_RADIUS * RESERVOIR_RADIUS * waterLevel / 1000.0;
}

void checkAndAdjustPH(unsigned long currentTime) {
  lastpHCheckTime = currentTime;
  float pH = readpH();
  Serial.printf("Current pH: %.2f\n", pH);

  if (pH < PH_LOWER_LIMIT || pH > PH_UPPER_LIMIT) {
    if (pH < PH_TARGET) {
      Serial.println("pH too low, activating base pump");
      digitalWrite(BASE_PUMP_RELAY, LOW);
    } else {
      Serial.println("pH too high, activating acid pump");
      digitalWrite(ACID_PUMP_RELAY, LOW);
    }
    isPHAdjusting = true;
    
    Serial.printf("Dosing for %ld ms based on current reservoir volume\n", ph_pump_duration);
  } else {
    Serial.println("pH within acceptable range");
  }
}

// Modify handleRoot() with a modern, cleaner interface
void handleRoot() {
  server.sendHeader("Cache-Control", "max-age=31536000");
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Aeroponic Control System</title>
    <link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&display=swap">
    <style>
        :root {
            --primary-gradient-start: #84fab0;
            --primary-gradient-end: #8fd3f4;
            --card-background: rgba(255, 255, 255, 0.9);
            --text-primary: #2c3e50;
            --text-secondary: #5a7a94;
            --success-color: #059669;
            --warning-color: #d97706;
            --shadow-color: rgba(0, 0, 0, 0.1);
        }

        body {
            font-family: Arial, sans-serif;
            background: linear-gradient(120deg, var(--primary-gradient-start) 0%, var(--primary-gradient-end) 100%);
            margin: 0;
            padding: 20px;
            color: var(--text-primary);
            min-height: 100vh;
        }

        .container {
            max-width: 1200px;
            margin: 2rem auto;
            padding: 0 1rem;
        }

        .header {
            text-align: center;
            margin-bottom: 2rem;
        }

        .header h1 {
            font-size: 2.25rem;
            font-weight: 600;
            color: var(--text-primary);
            margin-bottom: 0.5rem;
        }

        .header p {
            color: var(--text-secondary);
        }

        .dashboard {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 1.5rem;
            margin-bottom: 2rem;
        }

        .card {
            background: var(--card-background);
            border-radius: 5px;
            padding: 15px;
            text-align: center;
            box-shadow: 0 2px 5px var(--shadow-color);
            transition: transform 0.2s ease;
        }

        .card:hover {
            transform: translateY(-2px);
        }

        .card h3 {
            font-size: 1rem;
            font-weight: 500;
            color: var(--text-secondary);
            margin-bottom: 0.5rem;
        }

        .card .value {
            font-size: 1.875rem;
            font-weight: 600;
            color: var(--text-primary);
        }

        .card .unit {
            font-size: 0.875rem;
            color: var(--text-secondary);
            margin-left: 0.25rem;
        }

        .system-status {
            background: var(--card-background);
            border-radius: 5px;
            padding: 20px;
            margin-bottom: 20px;
            box-shadow: 0 2px 5px var(--shadow-color);
        }

        .system-status h2 {
            font-size: 1.25rem;
            font-weight: 600;
            margin-bottom: 1.5rem;
            color: var(--text-primary);
        }

        .status-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 1rem;
        }

        .status-card {
            display: flex;
            align-items: center;
            padding: 1rem;
            background: var(--background-color);
            border-radius: 0.5rem;
            transition: transform 0.2s ease;
        }

        .status-card:hover {
            transform: translateY(-2px);
        }

        .status-icon {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 1rem;
            background-color: var(--text-secondary);
        }

        .status-icon.active {
            background-color: var(--success-color);
            box-shadow: 0 0 12px rgba(5, 150, 105, 0.4);
        }

        .status-icon.warning {
            background-color: var(--warning-color);
            box-shadow: 0 0 12px rgba(217, 119, 6, 0.4);
        }

        .status-text {
            font-size: 0.875rem;
            font-weight: 500;
            color: var(--text-primary);
        }
    </style>
</head>
<body>
    <div class="container">
        <header class="header">
            <h1>Aeroponic Control System</h1>
            <p>Real-time monitoring and control dashboard</p>
        </header>

        <div class="dashboard">
            <div class="card">
                <h3>Temperature</h3>
                <div class="value" id="temp">--<span class="unit">°C</span></div>
            </div>
            <div class="card">
                <h3>Humidity</h3>
                <div class="value" id="hum">--<span class="unit">%</span></div>
            </div>
            <div class="card">
                <h3>pH Level</h3>
                <div class="value" id="ph">--</div>
            </div>
            <div class="card">
                <h3>Reservoir Volume</h3>
                <div class="value" id="rv">--<span class="unit">L</span></div>
            </div>
            <div class="card">
                <h3>Light Intensity</h3>
                <div class="value" id="li">--<span class="unit">lux</span></div>
            </div>
        </div>

        <div class="system-status">
            <h2>System Status</h2>
            <div class="status-grid">
                <div class="status-card" id="misting-status">
                    <div class="status-icon"></div>
                    <span class="status-text">Misting System Idle</span>
                </div>
                <div class="status-card" id="rotation-status">
                    <div class="status-icon"></div>
                    <span class="status-text">Rotation System Idle</span>
                </div>
                <div class="status-card" id="ph-status">
                    <div class="status-icon"></div>
                    <span class="status-text">pH System Stable</span>
                </div>
            </div>
        </div>
    </div>

    <script>
        const updateSensorData = async () => {
            try {
                const response = await fetch('/data');
                const data = await response.json();
                
                const updateValue = (id, value) => {
                    const element = document.getElementById(id);
                    if (element) element.textContent = value;
                };

                updateValue('temp', data.Temperature);
                updateValue('hum', data.Humidity);
                updateValue('ph', data.pH);
                updateValue('rv', data.ReservoirVolume);
                updateValue('li', data.LightIntensity);

                // Update system status indicators
                const updateStatus = (id, isActive, text) => {
                    const card = document.getElementById(id);
                    if (card) {
                        const icon = card.querySelector('.status-icon');
                        const textEl = card.querySelector('.status-text');
                        icon.className = 'status-icon ' + (isActive ? 'active' : '');
                        textEl.textContent = text;
                    }
                };

                // Misting System Status
                updateStatus('misting-status', 
                    data.isMisting,
                    data.isMisting ? 'Misting System Active' : 'Misting System Idle'
                );

                // Rotation System Status
                updateStatus('rotation-status',
                    data.isRotating,
                    data.isRotating ? 'System Rotating' : 'Rotation System Idle'
                );

                // pH System Status
                const phStatusText = {
                    'stable': 'pH System Stable',
                    'adjusting': 'pH Adjustment in Progress',
                    'completed': 'pH Adjustment Complete'
                };
                updateStatus('ph-status',
                    data.phStatus !== 'stable',
                    phStatusText[data.phStatus] || 'pH System Stable'
                );
            } catch (error) {
                console.error('Error fetching sensor data:', error);
            }
        };

        // Initialize
        updateSensorData();
        setInterval(updateSensorData, 2000);
    </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

// Optimize handleData() to reduce JSON processing
void handleData() {
  unsigned long currentTime = millis();
  if (currentTime - lastDataUpdate < DATA_UPDATE_INTERVAL) {
    server.send(304); // Not Modified
    return;
  }
  
  lastDataUpdate = currentTime;
  
  String jsonString = "{";
  jsonString += "\"Temperature\":\"" + String(temperature, 1) + " °C\",";
  jsonString += "\"Humidity\":\"" + String(humidity, 1) + " %\",";
  jsonString += "\"pH\":\"" + String(pH, 2) + "\",";
  jsonString += "\"ReservoirVolume\":\"" + String(reservoirVolume, 1) + " L\",";
  jsonString += "\"LightIntensity\":\"" + String(lightIntensity) + "\"";
  jsonString += "}";
  
  server.sendHeader("Cache-Control", "max-age=1");
  server.send(200, "application/json", jsonString);
}

// Optimize handleControl() to prevent rapid-fire requests
void handleControl() {
  static unsigned long lastControlUpdate = 0;
  if (millis() - lastControlUpdate < 100) {
    server.send(429, "text/plain", "Too Many Requests");
    return;
  }
  
  if (server.hasArg("plain")) {
    lastControlUpdate = millis();
    String message;
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }
    
    if (doc.containsKey("lightThreshold")) {
      LIGHT_THRESHOLD = doc["lightThreshold"];
      message = "Light threshold set to: " + String(LIGHT_THRESHOLD);
    } else if (doc.containsKey("pHTarget")) {
      PH_TARGET = doc["pHTarget"];
      message = "pH target set to: " + String(PH_TARGET);
    }
    
    server.send(200, "text/plain", message);
  }
}

// Add this function to monitor AP connections (add after setup())
void checkNewClients() {
  static int lastClientCount = 0;
  int currentClientCount = WiFi.softAPgetStationNum();
  
  if (currentClientCount != lastClientCount) {
    Serial.print("Number of connected clients: ");
    Serial.println(currentClientCount);
    lastClientCount = currentClientCount;
  }
}