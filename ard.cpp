#include "Adafruit_SHT31.h"
#include <Wire.h>
#include <math.h>
#include <AccelStepper.h>

// Pin Definitions
#define DHT_PIN 3
#define PH_PIN A2
#define VPD_PUMP_RELAY 9
#define ACID_PUMP_RELAY 8
#define BASE_PUMP_RELAY 7
#define MIX_PUMP_RELAY 2
#define TRIG_PIN 5
#define ECHO_PIN 4
#define LDR_PIN A1
#define STEPPER_STEP_PIN 12
#define STEPPER_DIR_PIN 13

// Constants
#define DHTTYPE DHT11
#define VPD_PUMP_DURATION 5000
#define MIX_PUMP_DURATION 1000
#define PH_CHECK_INTERVAL 30000
#define PH_WAIT_INTERVAL 18000
#define PH_LOWER_LIMIT 5.5
#define PH_UPPER_LIMIT 6.5
#define DOSAGE_RATE 0.00025 // 1 ml per 4 liters
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
unsigned long vpdCycleInterval = 1200; // 2 minutes default
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
int lightIntensity = 0;
int LIGHT_THRESHOLD = 300;
float PH_TARGET = 6.0;

void setup() {
  Serial.begin(9600);
  
  pinMode(VPD_PUMP_RELAY, OUTPUT);
  pinMode(ACID_PUMP_RELAY, OUTPUT);
  pinMode(BASE_PUMP_RELAY, OUTPUT);
  pinMode(MIX_PUMP_RELAY, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  digitalWrite(VPD_PUMP_RELAY, HIGH);
  digitalWrite(ACID_PUMP_RELAY, HIGH);
  digitalWrite(BASE_PUMP_RELAY, HIGH);
  digitalWrite(MIX_PUMP_RELAY, HIGH);
  
   if (! sht31.begin(0x44)) {   // Set to 0x45 for alternate I2C address
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
  lightIntensity = analogRead(LDR_PIN);

  handleVPDControl(currentTime);
  handlePHControl(currentTime);
  checkReservoirVolume(currentTime);
  checkLightAndRotate(currentTime);
  
  stepper.run();
}

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
      Serial.println("Failed to read from DHT sensor!");
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
    
    Serial.print("Volume: ");
    Serial.print(volume, 1);
    Serial.println(" liters");

    ph_pump_duration = volume * DOSAGE_RATE * 1000000; // Convert to ms
  }
}

void checkLightAndRotate(unsigned long currentTime) {
  if (currentTime - lastRotationTime >= ROTATION_INTERVAL) {
    lastRotationTime = currentTime;
    
    int lightIntensity = analogRead(LDR_PIN);
    Serial.print("Light intensity = ");
    Serial.println(lightIntensity);

    if (lightIntensity > LIGHT_THRESHOLD) {
      stepper.moveTo(stepper.currentPosition() + STEPS_90_DEGREES);
      while (stepper.distanceToGo() != 0) {
        stepper.run();
      }
      Serial.println("Rotated 90 degrees");
    } else {
      Serial.println("Light intensity below threshold, not rotating");
    }
  }
}

float readpH() {
  int sensorValue = analogRead(PH_PIN);
  return map(sensorValue, 0, 1023, 0, 14);
}

float calculateVPD(float temperature, float humidity) {
  float svp = 0.6108 * exp(17.27 * temperature / (temperature + 237.3)); 
  float avp = (humidity / 100.0) * svp;
  return svp - avp;
}

void updateVPDCycleInterval(float vpd) {
  vpdCycleInterval = (vpd > 1.5) ? 6000 : (vpd < 0.8) ? 18000 : 12000;
  Serial.print("New VPD cycle interval: ");
  Serial.print(vpdCycleInterval / 1000);
  Serial.println(" seconds");
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
  Serial.print("Current pH: ");
  Serial.println(pH, 2);

  if (pH < PH_LOWER_LIMIT || pH > PH_UPPER_LIMIT) {
    if (pH < PH_TARGET) {
      Serial.println("pH too low, activating base pump");
      digitalWrite(BASE_PUMP_RELAY, LOW);
    } else {
      Serial.println("pH too high, activating acid pump");
      digitalWrite(ACID_PUMP_RELAY, LOW);
    }
    isPHAdjusting = true;
    
    Serial.print("Dosing for ");
    Serial.print(ph_pump_duration);
    Serial.println(" ms based on current reservoir volume");
  } else {
    Serial.println("pH within acceptable range");
  }
}
