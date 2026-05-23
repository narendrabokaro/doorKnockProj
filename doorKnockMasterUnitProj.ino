#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiManager.h> // Integrated for dynamic Wi-Fi setup
#include <WiFiUdp.h>
#include <NTPClient.h>   // Integrated for automatic sleep schedule
#include <RCSwitch.h> 

RCSwitch mySwitch = RCSwitch();

// --- Configuration & Secret Keys ---
const String apiKey = "97d26ad5-079e-XXXX-XXXX";
const String phoneNumber = "91991603XXXXX";
const String messageText = "Knock knock! Someone is at the door.";

// Pin Definitions
const int sensorPowerBusPin = 5; // D1 - Controls Transistor Base for Sensor Power/GND Bus
const int sensorPin = 14;        // D5 - Knock/Vibration Sensor Input
const int statusLed = 16;        // D0 - Heartbeat/Status LED
const int rfTransmitPin = 15;    // D8 - 433MHz Transmitter Data

// Timers and Logic
unsigned long lastTriggerTime = 0;
const int cooldownTimer = 30000;      // 30 seconds
unsigned long lastHeartbeat = 0;
const int heartbeatInterval = 10000;  // 10 seconds

// NTP Setup (Austin, Texas Time)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -18000); 

// Schedule Tracking Flags
bool sentSleepCommand = false;
bool sentWakeCommand = false;

// LED State Variables
bool isLedActive = false;
unsigned long ledStartTime = 0;
const unsigned long processingWindow = 5000; // 5 seconds solid LED

// Interrupt & Density Variables
volatile bool knockDetected = false;
volatile int pulseCount = 0;
unsigned long windowStartTime = 0; 
const int pulseWindow = 70;        // 70ms window for density check
const int minPulsesForKnock = 8;   // Threshold to filter false positives

void IRAM_ATTR handleSensorPulse() {
  // Only register pulses if the system is supposed to be awake (Double protection)
  if (!sentSleepCommand) {
    pulseCount++;
    knockDetected = true; 
  }
}

void setup() {
  Serial.begin(115200);
  
  // Initialize Pins
  pinMode(sensorPowerBusPin, OUTPUT);
  digitalWrite(sensorPowerBusPin, HIGH); // Turn on power bus immediately at boot
  
  pinMode(statusLed, OUTPUT);
  pinMode(sensorPin, INPUT_PULLUP); 
  attachInterrupt(digitalPinToInterrupt(sensorPin), handleSensorPulse, FALLING);

  mySwitch.enableTransmit(rfTransmitPin); // Initialize 433MHz on D8

  // WiFiManager Setup - Replaces hardcoded Wi-Fi credentials
  WiFiManager wifiManager;
  Serial.println("Connecting to Wi-Fi via WiFiManager...");
  
  // If it can't find saved credentials, it opens an AP named "KnockMaster-Setup"
  if (!wifiManager.autoConnect("KnockMaster-Setup")) {
    Serial.println("Failed to connect and hit timeout. Resetting MCU...");
    delay(3000);
    ESP.reset();
  }
  
  Serial.println("\nWiFi connected dynamically!");
  
  // Start the internet clock
  timeClient.begin();
}

void sendWhatabotPOST() {
  // 1. Radio Blast to Slave Unit
  for(int i = 0; i < 5; i++) {
    mySwitch.send(1001, 24); 
    delay(10); 
  }

  // 2. WhatsApp Notification via Whatabot
  isLedActive = true;
  ledStartTime = millis();
  digitalWrite(statusLed, HIGH);

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  
  HTTPClient https;
  const char* serverUrl = "https://api.whatabot.io/Whatsapp/RequestSendMessage";
  if (https.begin(*client, serverUrl)) {
    https.addHeader("Content-Type", "application/json");
    String jsonPayload = "{\"ApiKey\":\"" + apiKey + "\",\"Phone\":\"" + phoneNumber + "\",\"Text\":\"" + messageText + "\"}";
    Serial.println("Sending WhatsApp Notification...");
    int httpResponseCode = https.POST(jsonPayload);
    
    if (httpResponseCode > 0) {
      Serial.printf("[HTTP] Success: %d\n", httpResponseCode);
    } else {
      Serial.printf("[HTTP] Failed: %s\n", https.errorToString(httpResponseCode).c_str());
    }
    https.end();
  }
}

void loop() {
  // Update NTP Client time tracking
  timeClient.update();
  unsigned long currentTime = millis();

  // 1. NON-BLOCKING LED HANDLER
  if (isLedActive) {
    if (currentTime - ledStartTime >= processingWindow) {
      digitalWrite(statusLed, LOW);
      isLedActive = false;
    }
  }

  // 2. HEARTBEAT LED (Flashes quickly if connected and system is awake)
  if (!isLedActive && (currentTime - lastHeartbeat > heartbeatInterval)) {
    if (WiFi.status() == WL_CONNECTED && !sentSleepCommand) {
      digitalWrite(statusLed, HIGH);
      delay(50); 
      digitalWrite(statusLed, LOW);
    }
    lastHeartbeat = currentTime;
  }

  // 3. SLEEP SCHEDULE LOGIC CONTROLLER
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  // 11:00 PM (23:00) -> Cut Power Bus and Send Radio Sleep Command
  if (currentHour == 23 && currentMinute == 0) {
    if (!sentSleepCommand) {
      Serial.println("11:00 PM: Cutting power to sensor bus & muting slave unit...");
      digitalWrite(sensorPowerBusPin, LOW); // Transistor turns off -> Kills the green LED!
      
      // Blast sleep command to slave
      for(int i = 0; i < 5; i++) {
        mySwitch.send(9999, 24);
        delay(10);
      }
      
      sentSleepCommand = true;
      sentWakeCommand = false;
    }
  }

  // 6:00 AM (06:00) -> Restore Power Bus and Send Radio Wake Command
  if (currentHour == 6 && currentMinute == 0) {
    if (!sentWakeCommand) {
      Serial.println("6:00 AM: Restoring power to sensor bus & waking up slave unit...");
      digitalWrite(sensorPowerBusPin, HIGH); // Transistor turns on -> Sensor power rail alive
      
      // Blast wake command to slave
      for(int i = 0; i < 5; i++) {
        mySwitch.send(1111, 24);
        delay(10);
      }
      
      sentWakeCommand = true;
      sentSleepCommand = false;
    }
  }

  // 4. KNOCK DETECTION WITH PULSE DENSITY FILTER (Only processes if awake)
  if (knockDetected && !sentSleepCommand) {
    if (windowStartTime == 0) {
      windowStartTime = currentTime;
    }

    if (currentTime - windowStartTime >= pulseWindow) {
      Serial.printf("Pulses detected: %d\n", pulseCount);

      if (pulseCount >= minPulsesForKnock && (currentTime - lastTriggerTime > cooldownTimer)) {
        Serial.println("Density Verified: Valid Knock Burst!");
        sendWhatabotPOST();
        lastTriggerTime = currentTime;
      }

      // Reset density evaluation states
      pulseCount = 0;
      knockDetected = false;
      windowStartTime = 0;
    }
  }
}
