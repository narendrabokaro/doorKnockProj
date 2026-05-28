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

// --- NEW: Quiet Hours Configuration (24-Hour Format) ---
const int sleepHour = 23;        // 11:00 PM - Enter Stealth Sleep Mode
const int wakeHour = 6;          // 6:00 AM  - Restore Power and Re-arm

// Pin Definitions
const int sensorPowerBusPin = 5; // D1 - Controls Transistor Base for Sensor Power/GND Bus
const int sensorPin = 14;        // D5 - Knock/Vibration Sensor Input
const int statusLed = 16;        // D0 - Heartbeat/Status LED
const int rfTransmitPin = 15;    // D8 - 433MHz Transmitter Data
const int masterButtonPin = 0;   // D3 (GPIO0) - Physical Manual Test Button

// Timers and Logic
unsigned long lastTriggerTime = 0;
const int cooldownTimer = 30000;      // 30 seconds
unsigned long lastHeartbeat = 0;
const int heartbeatInterval = 10000;  // 10 seconds
unsigned long lastNTPCheck = 0;
const unsigned long ntpInterval = 1000; // Check time once every 1000ms (1 second)

// Debounce Timer for the Master Test Button
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

// Interrupt & Density Variables (HIGH SENSITIVITY CONFIG)
volatile bool knockDetected = false;
volatile int pulseCount = 0;
unsigned long windowStartTime = 0; 
const int pulseWindow = 120;        // Open window to catch light, slow vibrations
const int minPulsesForKnock = 4;   // Low threshold to capture subtle taps

// Environmental Noise Pattern Filtering Variables
int detectedPacketCount = 0;       // Tracks how many distinct knock bursts have occurred
unsigned long firstPacketTime = 0;   // Timestamp of the first validated burst
const unsigned long packetResetWindow = 1500; // 1.5 seconds to complete a multi-tap knock pattern
unsigned long packetIntermissionTimeout = 0; // Guard to separate distinct taps

void IRAM_ATTR handleSensorPulse() {
  pulseCount++;
  knockDetected = true; 
}

void setup() {
  Serial.begin(115200);
  
  // Initialize Pins
  pinMode(sensorPowerBusPin, OUTPUT);
  digitalWrite(sensorPowerBusPin, HIGH); 
  
  pinMode(statusLed, OUTPUT);
  pinMode(sensorPin, INPUT_PULLUP); 
  pinMode(masterButtonPin, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(sensorPin), handleSensorPulse, FALLING);

  mySwitch.enableTransmit(rfTransmitPin); 
  mySwitch.setRepeatTransmit(12); 

  // WiFiManager Setup
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  
  Serial.println("Connecting to Wi-Fi via WiFiManager...");
  
  if (!wifiManager.autoConnect("KnockMaster-Setup")) {
    Serial.println("Failed to connect or portal timed out. Resetting MCU to retry...");
    delay(3000);
    ESP.reset();
  }
  
  Serial.println("\nWiFi connected dynamically via Portal!");
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
  // Dynamically uses variables defined at the top
  if (currentHour == sleepHour || (currentHour >= 0 && currentHour < wakeHour)) {
    if (!sentSleepCommand) {
      Serial.printf("%02d:00 Window Active: Entering Stealth Sleep Mode...\n", sleepHour);
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
    detectedPacketCount = 0;
    return; 
  } 
  else {
    if (!sentWakeCommand) {
      Serial.printf("%02d:00 Window Active: Restoring power and re-arming sensor...\n", wakeHour);
      digitalWrite(sensorPowerBusPin, HIGH); 
      knockDetected = false;
      pulseCount = 0;
      windowStartTime = 0;
      detectedPacketCount = 0;
      attachInterrupt(digitalPinToInterrupt(sensorPin), handleSensorPulse, FALLING);
      mySwitch.send(1111, 24);
      sentWakeCommand = true;
      sentSleepCommand = false; 
    }
  }

  // =========================================================================
  // 3. DAYTIME ACTIVITIES 
  // =========================================================================

  // Manual Test Button Handler (Active LOW)
  if (digitalRead(masterButtonPin) == LOW) {
    if (currentTime - lastButtonPressTime >= buttonDebounceDelay) {
      lastButtonPressTime = currentTime;
      Serial.println("Master Test Button Pressed! Blasting verification code (1001) to Slave...");
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

  // Reset multi-tap evaluation sequence if the 1.5-second human intent window expires
  if (detectedPacketCount > 0 && (currentTime - firstPacketTime > packetResetWindow)) {
    Serial.println("Pattern Expired: Isolated environmental noise or single vibration dropped.");
    detectedPacketCount = 0;
  }

  // Knock Detection with Multi-Packet Behavioral Filter
  if (knockDetected) {
    if (windowStartTime == 0) {
      windowStartTime = currentTime;
    }

    if (currentTime - windowStartTime >= pulseWindow) {
      if (pulseCount >= minPulsesForKnock && (currentTime - lastTriggerTime > cooldownTimer)) {
        
        if (currentTime > packetIntermissionTimeout) {
          detectedPacketCount++;
          packetIntermissionTimeout = currentTime + 150; 
          
          if (detectedPacketCount == 1) {
            firstPacketTime = currentTime; 
            Serial.println("First light tap signature caught! Waiting for companion tap...");
          }
          
          if (detectedPacketCount >= 2) {
            Serial.printf("Pattern Verified: %d distinct light taps detected! Valid Human Knock.\n", detectedPacketCount);
            sendWhatabotPOST();
            lastTriggerTime = currentTime;
            detectedPacketCount = 0; 
          }
        }
      }

      pulseCount = 0;
      knockDetected = false;
      windowStartTime = 0;
    }
  }
}
