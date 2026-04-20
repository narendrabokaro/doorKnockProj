#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

// --- Configuration ---
const char* ssid = "TP-LINK_91BB";
const char* password = "84676597";
const String apiKey = "97d26ad5-079e-477b-8d85";
const String phoneNumber = "919916035532";
const String messageText = "Knock knock! Someone is at the door.";

// Pin Definitions
const int sensorPin = 14; // D5
const int statusLed = 16; // D0

// Timers and Logic
unsigned long lastTriggerTime = 0;
const int cooldownTimer = 30000; // 30 seconds
unsigned long lastHeartbeat = 0;
const int heartbeatInterval = 10000; // 10 seconds

// LED State Variables
bool isLedActive = false;
unsigned long ledStartTime = 0;
const unsigned long processingWindow = 5000; // 5 seconds solid LED

// Interrupt Variables
volatile bool knockDetected = false;
volatile int pulseCount = 0;
unsigned long lastPulseTime = 0;

// Function called by Hardware Interrupt
void IRAM_ATTR handleSensorPulse() {
  unsigned long now = millis();
  // Filter noise: pulses must be at least 20ms apart to be counted
  if (now - lastPulseTime > 20) {
    pulseCount++;
    lastPulseTime = now;
    knockDetected = true; 
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(statusLed, OUTPUT);
  // Use Interrupt to catch fast vibrations
  pinMode(sensorPin, INPUT_PULLUP); 
  attachInterrupt(digitalPinToInterrupt(sensorPin), handleSensorPulse, FALLING);

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
  // Start the LED timer
  isLedActive = true;
  ledStartTime = millis();
  digitalWrite(statusLed, HIGH); // LED Solid ON for processing

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

  // 2. HEARTBEAT (Only if no knock is being processed)
  if (!isLedActive && (currentTime - lastHeartbeat > heartbeatInterval)) {
    if (WiFi.status() == WL_CONNECTED) {
      digitalWrite(statusLed, HIGH);
      delay(50); 
      digitalWrite(statusLed, LOW);
    }
    lastHeartbeat = currentTime;
  }

  // 3. KNOCK LOGIC (Requiring at least 2 pulses within 500ms for a "Burst")
  if (knockDetected) {
    if (currentTime - lastPulseTime > 500) {
      pulseCount = 0;
      knockDetected = false;
    }

    // Trigger only if we see a burst (more than 1 pulse) and cooldown is over
    if (pulseCount >= 2 && (currentTime - lastTriggerTime > cooldownTimer)) {
      Serial.println("Valid Knock Burst Verified!");
      sendWhatabotPOST();
      lastTriggerTime = currentTime;
      pulseCount = 0;
      knockDetected = false;
    }
  }
}
