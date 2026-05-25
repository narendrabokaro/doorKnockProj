#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiManager.h> 
#include <WiFiUdp.h>
#include <NTPClient.h>   
#include <RCSwitch.h> 

RCSwitch mySwitch = RCSwitch();

// --- Configuration & Secret Keys ---
const String apiKey = "97d26ad5-079e-xxxx-xxxx";
const String phoneNumber = "91991603xxxx";
const String messageText = "Knock knock! Someone is at the door.";

// Pin Definitions
const int sensorPowerBusPin = 5; // D1 - Controls Transistor Base for Sensor Power/GND Bus
const int sensorPin = 14;        // D5 - Knock/Vibration Sensor Input
const int statusLed = 16;        // D0 - Heartbeat/Status LED
const int rfTransmitPin = 15;    // D8 - 433MHz Transmitter Data
const int masterButtonPin = 0;   // D3 (GPIO0) - NEW: Physical Manual Test Button

// Timers and Logic
unsigned long lastTriggerTime = 0;
const int cooldownTimer = 30000;      // 30 seconds
unsigned long lastHeartbeat = 0;
const int heartbeatInterval = 10000;  // 10 seconds
unsigned long lastNTPCheck = 0;
const unsigned long ntpInterval = 1000; // Check time once every 1000ms (1 second)

// NEW: Debounce Timer for the Master Test Button
unsigned long lastButtonPressTime = 0;
const unsigned long buttonDebounceDelay = 500; // 500ms debounce guard

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
  pulseCount++;
  knockDetected = true; 
}

void setup() {
  Serial.begin(115200);
  
  // Initialize Pins
  pinMode(sensorPowerBusPin, OUTPUT);
  digitalWrite(sensorPowerBusPin, HIGH); // Turn on power bus immediately at boot
  
  pinMode(statusLed, OUTPUT);
  pinMode(sensorPin, INPUT_PULLUP); 
  
  // NEW: Initialize the Master physical test button with internal pull-up resistor
  pinMode(masterButtonPin, INPUT_PULLUP);
  
  // Attach interrupt initially for daytime boot
  attachInterrupt(digitalPinToInterrupt(sensorPin), handleSensorPulse, FALLING);

  // Configure transmitter and increase repetitions to overcome ambient noise
  mySwitch.enableTransmit(rfTransmitPin); 
  mySwitch.setRepeatTransmit(12); 

  // WiFiManager Setup - Replaces hardcoded Wi-Fi credentials
  WiFiManager wifiManager;
  Serial.println("Connecting to Wi-Fi via WiFiManager...");
  
  // If it can't find saved credentials, it opens an AP named "KnockMaster-Setup"
  if (!wifiManager.autoConnect("KnockMaster-Setup")) {
    Serial.println("Failed to connect and hit timeout. Resetting MCU...");
    delay(3000);
    ESP.reset();
  }
  
  Serial.println("\nWiFi connected dynamically via Portal!");
  
  // Start the internet clock
  timeClient.begin();
}

void sendWhatabotPOST() {
  // 1. Radio Blast to Slave Unit (Redundant local backup)
  for(int i = 0; i < 5; i++) {
    mySwitch.send(1001, 24); 
    delay(10); 
  }

  // 2. WhatsApp Notification via Whatabot API
  isLedActive = true;
  ledStartTime = millis();
  digitalWrite(statusLed, HIGH); // Turn status LED solid during web processing

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
  unsigned long currentTime = millis();

  // =========================================================================
  // 1. THROTTLED TIME EVALUATION
  // =========================================================================
  if (currentTime - lastNTPCheck >= ntpInterval) {
    lastNTPCheck = currentTime;
    timeClient.update();
  }
  
  int currentHour = timeClient.getHours();

  // =========================================================================
  // 2. TOP-OF-LOOP SLEEP GATEKEEPER
  // =========================================================================
  if (currentHour == 23 || (currentHour >= 0 && currentHour < 6)) {
    
    if (!sentSleepCommand) {
      Serial.println("11:00 PM Window Active: Entering Stealth Sleep Mode...");
      detachInterrupt(digitalPinToInterrupt(sensorPin));
      digitalWrite(sensorPowerBusPin, LOW); 
      digitalWrite(statusLed, LOW);         
      
      mySwitch.send(9999, 24);
      
      sentSleepCommand = true;
      sentWakeCommand = false; 
    }
    
    knockDetected = false;
    pulseCount = 0;
    windowStartTime = 0;
    
    return; // SHORT-CIRCUIT LOOP: Do absolutely nothing until 6:00 AM
  } 
  else {
    if (!sentWakeCommand) {
      Serial.println("6:00 AM Window Active: Restoring power and re-arming sensor...");
      digitalWrite(sensorPowerBusPin, HIGH); 
      
      knockDetected = false;
      pulseCount = 0;
      windowStartTime = 0;
      
      attachInterrupt(digitalPinToInterrupt(sensorPin), handleSensorPulse, FALLING);
      mySwitch.send(1111, 24);
      
      sentWakeCommand = true;
      sentSleepCommand = false; 
    }
  }

  // =========================================================================
  // 3. DAYTIME ACTIVITIES (Executed only outside the sleep window)
  // =========================================================================

  // NEW: Manual Test Button Handler (Active LOW)
  // Allows testing the RF link to the slave immediately without hitting WhatsApp APIs
  if (digitalRead(masterButtonPin) == LOW) {
    if (currentTime - lastButtonPressTime >= buttonDebounceDelay) {
      lastButtonPressTime = currentTime;
      Serial.println("Master Test Button Pressed! Blasting verification code (1001) to Slave...");
      
      // Send 5 quick bursts directly to the Slave unit to guarantee receipt
      for(int i = 0; i < 5; i++) {
        mySwitch.send(1001, 24); 
        delay(10); 
      }
    }
  }

  // Non-blocking solid LED processing window handler
  if (isLedActive) {
    if (currentTime - ledStartTime >= processingWindow) {
      digitalWrite(statusLed, LOW);
      isLedActive = false;
    }
  }

  // Daytime Heartbeat LED (50ms clean flash every 10 seconds)
  if (!isLedActive && (currentTime - lastHeartbeat > heartbeatInterval)) {
    if (WiFi.status() == WL_CONNECTED) {
      digitalWrite(statusLed, HIGH);
      delay(50); 
      digitalWrite(statusLed, LOW);
    }
    lastHeartbeat = currentTime;
  }

  // Knock Detection with Pulse Density Filter
  if (knockDetected) {
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

      pulseCount = 0;
      knockDetected = false;
      windowStartTime = 0;
    }
  }
}
