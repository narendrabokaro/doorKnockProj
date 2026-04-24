#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <RCSwitch.h> // Added for POC Phase 2

// --- Configuration ---
const char* ssid = "TP-LINK_91BB";
const char* password = "84676597";
const String apiKey = "97d26ad5-079e-477b-8d85";
const String phoneNumber = "919916035532";
const String messageText = "Knock knock! Someone is at the door.";

// Pin Definitions
const int sensorPin = 14; // D5
const int statusLed = 16; // D0
const int rfTransmitPin = 15; // D8 for 433MHz Transmitter

// Timers and Logic
unsigned long lastTriggerTime = 0;
const int cooldownTimer = 30000; // 30 seconds
unsigned long lastHeartbeat = 0;
const int heartbeatInterval = 10000; // 10 seconds

// LED State Variables
bool isLedActive = false;
unsigned long ledStartTime = 0;
const unsigned long processingWindow = 5000; // 5 seconds solid LED

// Interrupt & Density Variables
volatile bool knockDetected = false;
volatile int pulseCount = 0;
unsigned long lastPulseTime = 0;
unsigned long windowStartTime = 0; // New: To track the 50ms burst window
const int pulseWindow = 50;        // New: 50ms window for density check
const int minPulsesForKnock = 6;   // New: Threshold to filter false positives

RCSwitch mySwitch = RCSwitch(); // Added for Nano Slave communication

void IRAM_ATTR handleSensorPulse() {
  pulseCount++;
  knockDetected = true; 
}

void setup() {
  Serial.begin(115200);
  pinMode(statusLed, OUTPUT);
  pinMode(sensorPin, INPUT_PULLUP); 
  attachInterrupt(digitalPinToInterrupt(sensorPin), handleSensorPulse, FALLING);

  mySwitch.enableTransmit(rfTransmitPin); // Initialize 433MHz

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(statusLed, HIGH);
    delay(100); 
    digitalWrite(statusLed, LOW);
    delay(100);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
}

void sendWhatabotPOST() {
  // 1. Radio Blast to Arduino Nano Slave
  for(int i = 0; i < 5; i++) {
    mySwitch.send(1001, 24); 
    delay(10); 
  }

  // 2. WhatsApp Notification
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

  // 1. NON-BLOCKING LED HANDLER
  if (isLedActive) {
    if (currentTime - ledStartTime >= processingWindow) {
      digitalWrite(statusLed, LOW);
      isLedActive = false;
    }
  }

  // 2. HEARTBEAT
  if (!isLedActive && (currentTime - lastHeartbeat > heartbeatInterval)) {
    if (WiFi.status() == WL_CONNECTED) {
      digitalWrite(statusLed, HIGH);
      delay(50); 
      digitalWrite(statusLed, LOW);
    }
    lastHeartbeat = currentTime;
  }

  // 3. UPDATED KNOCK LOGIC (Pulse Density Filter)
  if (knockDetected) {
    // Start the timing window on the very first pulse
    if (windowStartTime == 0) {
      windowStartTime = currentTime;
    }

    // After 50ms, evaluate if it was a real knock or just noise
    if (currentTime - windowStartTime >= pulseWindow) {
      Serial.printf("Pulses detected: %d\n", pulseCount);

      if (pulseCount >= minPulsesForKnock && (currentTime - lastTriggerTime > cooldownTimer)) {
        Serial.println("Density Verified: Valid Knock Burst!");
        sendWhatabotPOST();
        lastTriggerTime = currentTime;
      }

      // Reset window and pulse count
      pulseCount = 0;
      knockDetected = false;
      windowStartTime = 0;
    }
  }
}
