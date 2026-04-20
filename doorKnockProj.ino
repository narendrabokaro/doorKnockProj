#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

// --- Configuration ---
const char* ssid = "TP-LINK_91BB";
const char* password = "********";

// Whatabot Settings
const String apiKey = "*******";
const String phoneNumber = "919916035532"; // No '+' sign
const String messageText = "Knock knock! Someone is at the door.";

const int sensorPin = 14; // D5 (GPIO 14)
unsigned long lastTriggerTime = 0;
const int cooldownTimer = 30000; // 30 seconds
unsigned long lastHeartbeat = 0;
const int heartbeatInterval = 10000; // Pulse every 10 seconds
const int statusLed = 16; // D0 

void setup() {
  Serial.begin(115200);
  pinMode(statusLed, OUTPUT);
  pinMode(sensorPin, INPUT_PULLUP); 

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    // 1. Fast Flash for WiFi Setup
    digitalWrite(statusLed, HIGH);
    delay(100);
    digitalWrite(statusLed, LOW);
    delay(100);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  // Turn off after connection
  digitalWrite(statusLed, LOW); 
}

void sendWhatabotPOST() {
  unsigned long processStartTime = millis(); // Track when the processing started
  digitalWrite(statusLed, HIGH);             // 2. LED Solid ON for knock processing
  
  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();
  
  HTTPClient https;
  const char* serverUrl = "https://api.whatabot.io/Whatsapp/RequestSendMessage";
  
  if (https.begin(*client, serverUrl)) {
    https.addHeader("Content-Type", "application/json");
    String jsonPayload = "{\"ApiKey\":\"" + apiKey + "\",\"Phone\":\"" + phoneNumber + "\",\"Text\":\"" + messageText + "\"}";
    
    Serial.println("Sending POST request to Whatabot...");
    int httpResponseCode = https.POST(jsonPayload);
    
    if (httpResponseCode > 0) {
      Serial.printf("[HTTP] Success, code: %d\n", httpResponseCode);
      // 3. Success Signal: Long 2-second pulse after the 5s window
      // (Handled below)
    } else {
      // 4. Error Signal: 3 quick bursts
      for(int i=0; i<3; i++) {
        digitalWrite(statusLed, LOW); delay(100);
        digitalWrite(statusLed, HIGH); delay(100);
      }
      Serial.printf("[HTTP] POST failed, error: %s (Code: %d)\n", https.errorToString(httpResponseCode).c_str(), httpResponseCode);
    }
    https.end();

    // Ensure LED stays solid for at least 5 seconds total for the knock detection phase
    while (millis() - processStartTime < 5000) {
      delay(10); 
    }
    
    if (httpResponseCode > 0) {
        digitalWrite(statusLed, LOW); delay(500); 
        digitalWrite(statusLed, HIGH); delay(2000); // Success pulse
    }

  } else {
    Serial.println("[HTTP] Unable to connect to server");
    // Error Signal for connection failure
    for(int i=0; i<3; i++) {
      digitalWrite(statusLed, LOW); delay(100);
      digitalWrite(statusLed, HIGH); delay(100);
    }
  }

  digitalWrite(statusLed, LOW); // Final reset to OFF
}

void loop() {
  unsigned long currentTime = millis();

  // 5. HEARTBEAT: Brief pulse to show the system is alive and connected
  if (currentTime - lastHeartbeat > heartbeatInterval) {
    if (WiFi.status() == WL_CONNECTED) {
      // Very quick "blip" so it's not annoying at night
      digitalWrite(statusLed, HIGH);
      delay(50); 
      digitalWrite(statusLed, LOW);
    } else {
      // If WiFi is lost, do a double-blink to alert you
      for(int i=0; i<2; i++) {
        digitalWrite(statusLed, HIGH); delay(100);
        digitalWrite(statusLed, LOW); delay(100);
      }
    }
    lastHeartbeat = currentTime;
  }

  // SENSOR LOGIC
  if (digitalRead(sensorPin) == LOW) { 
    if (currentTime - lastTriggerTime > cooldownTimer) {
      Serial.println("Valid Knock Detected!");
      sendWhatabotPOST();
      lastTriggerTime = millis(); // Reset trigger time after the 5s LED window
    }
  }
}
