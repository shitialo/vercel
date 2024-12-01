#include <ESP8266WiFi.h>
#include "Adafruit_SHT31.h"
#include <Wire.h>
#include <math.h>
#include <AccelStepper.h>
#include <ArduinoJson.h>

// Pin Definitions for ESP8266
#define PH_PIN A0          // ESP8266's only analog pin
#define VPD_PUMP_RELAY D1  // GPIO5
#define ACID_PUMP_RELAY D2 // GPIO4
#define BASE_PUMP_RELAY D3 // GPIO0
#define MIX_PUMP_RELAY D4  // GPIO2
#define TRIG_PIN D5        // GPIO14
#define ECHO_PIN D6        // GPIO12
#define LDR_PIN D7        // Changed to digital pin for LDR module
#define STEPPER_STEP_PIN D8 // GPIO15
#define STEPPER_DIR_PIN D0  // GPIO16

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
bool isLightDetected = false;
float PH_TARGET = 6.0;

void setup() {
  Serial.begin(115200);  // ESP8266 typically uses 115200 baud
  Wire.begin(SDA, SCL);  // ESP8266 I2C pins: SDA (GPIO4/D2), SCL (GPIO5/D1)
  
  pinMode(VPD_PUMP_RELAY, OUTPUT);
  pinMode(ACID_PUMP_RELAY, OUTPUT);
  pinMode(BASE_PUMP_RELAY, OUTPUT);
  pinMode(MIX_PUMP_RELAY, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);  // Digital input for LDR module
  
  digitalWrite(VPD_PUMP_RELAY, HIGH);
  digitalWrite(ACID_PUMP_RELAY, HIGH);
  digitalWrite(BASE_PUMP_RELAY, HIGH);
  digitalWrite(MIX_PUMP_RELAY, HIGH);
  
  if (!sht31.begin(0x44)) {
    Serial.println("Couldn't find SHT31");
    while (1) delay(1);
  }
  
  stepper.setMaxSpeed(1000);
  stepper.setAcceleration(500);
}

void loop() {
  unsigned long currentTime = millis();

  // Read sensor data
  temperature = sht31.readTemperature();
  humidity = sht31.readHumidity();
  vpd = calculateVPD(temperature, humidity);
  pH = readpH();
  waterLevel = measureWaterLevel();
  reservoirVolume = calculateReservoirVolume(waterLevel);
  isLightDetected = digitalRead(LDR_PIN);  // Digital read for LDR module

  handleVPDControl(currentTime);
  handlePHControl(currentTime);
  checkReservoirVolume(currentTime);
  checkLightAndRotate(currentTime);
  
  stepper.run();
  
  // Optional: Add small delay to prevent WDT reset
  yield();
}

// Modified checkLightAndRotate function for digital LDR
void checkLightAndRotate(unsigned long currentTime) {
  if (currentTime - lastRotationTime >= ROTATION_INTERVAL) {
    lastRotationTime = currentTime;
    
    bool isLight = digitalRead(LDR_PIN);  // HIGH when light is detected
    Serial.print("Light detected: ");
    Serial.println(isLight ? "Yes" : "No");

    if (isLight) {
      stepper.moveTo(stepper.currentPosition() + STEPS_90_DEGREES);
      while (stepper.distanceToGo() != 0) {
        stepper.run();
        yield();  // Allow ESP8266 to handle background tasks
      }
      Serial.println("Rotated 90 degrees");
    } else {
      Serial.println("Insufficient light, not rotating");
    }
  }
}

// The following functions remain largely unchanged from the original code
// Only adding yield() where necessary for ESP8266

void handleVPDControl(unsigned long currentTime) {
  if (currentTime - lastVPDCycleTime >= vpdCycleInterval) {
    lastVPDCycleTime = currentTime;
    
    float humidity = sht31.readHumidity();
    float temperature = sht31.readTemperature();

    if (!isnan(humidity) && !isnan(temperature)) {
      float vpd = calculateVPD(temperature, humidity);
      updateVPDCycleInterval(vpd);
      
      Serial.print("Humidity: ");
      Serial.print(humidity, 1);
      Serial.print("%, Temperature: ");
      Serial.print(temperature, 1);
      Serial.print("°C, VPD: ");
      Serial.print(vpd, 2);
      Serial.println(" kPa");
    } else {
      Serial.println("Failed to read from SHT31 sensor!");
    }

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
    yield();  // Allow ESP8266 to handle system tasks
    
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

float readpH() {
  int sensorValue = analogRead(PH_PIN);
  // ESP8266 ADC is 10-bit (0-1023)
  return map(sensorValue, 0, 1023, 0, 14);
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
  