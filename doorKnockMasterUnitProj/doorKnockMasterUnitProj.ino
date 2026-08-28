#include <Wire.h>

#define PCF_ADDRESS 0x20
#define SDA_PIN     D5  
#define SCL_PIN     D7  
#define INT_PIN     D2  

#define BUTTON_PIN      0   // P0
#define LED_PIN         1   // P1
#define BUZZER_PIN      2   // P2
#define CAM_TRIGGER_PIN 3   // P3 -> Connect to ESP32-CAM GPIO 13

volatile bool interruptTriggered = false;

// 1 = INPUT mode -OR- Output OFF
// 0 = Output ON (Sinking to GND)
uint8_t expanderState = 0xFF; 

IRAM_ATTR void handleInterrupt() {
  interruptTriggered = true;
}

void setup() {
  Serial.begin(115200);
  delay(2000); 
  Serial.println("\n--- PCF8574 Doorbell & ESP32-CAM Trigger ---");

  Wire.begin(SDA_PIN, SCL_PIN);
  
  pinMode(INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INT_PIN), handleInterrupt, FALLING);

  // Send the initial state (all high/off)
  Wire.beginTransmission(PCF_ADDRESS);
  Wire.write(expanderState); 
  Wire.endTransmission();
  
  // Dummy read to clear startup triggers
  Wire.requestFrom(PCF_ADDRESS, 1);
  if (Wire.available()) {
    Wire.read();
  }
  
  Serial.println("System Ready. Press the button on P0...");
}

void triggerAlert() {
  Serial.println("Executing Alert Sequence & Triggering ESP32-CAM...");

  // 1. Send active-LOW pulse to ESP32-CAM (P3 = 0)
  expanderState &= ~(1 << CAM_TRIGGER_PIN);
  Wire.beginTransmission(PCF_ADDRESS);
  Wire.write(expanderState);
  Wire.endTransmission();
  
  delay(50); // Pulse width

  // Release trigger (P3 = 1)
  expanderState |= (1 << CAM_TRIGGER_PIN);
  Wire.beginTransmission(PCF_ADDRESS);
  Wire.write(expanderState);
  Wire.endTransmission();

  // 2. Flash LED and sound Buzzer 3 times
  for (int i = 0; i < 3; i++) {
    // Turn ON: Clear bits to 0
    expanderState &= ~(1 << LED_PIN);
    expanderState &= ~(1 << BUZZER_PIN);
    Wire.beginTransmission(PCF_ADDRESS);
    Wire.write(expanderState);
    Wire.endTransmission();
    
    delay(150); // ON duration
    
    // Turn OFF: Set bits to 1
    expanderState |= (1 << LED_PIN);
    expanderState |= (1 << BUZZER_PIN);
    Wire.beginTransmission(PCF_ADDRESS);
    Wire.write(expanderState);
    Wire.endTransmission();
    
    delay(150); // OFF duration
  }
}

void loop() {
  // Watchdog check
  if (digitalRead(INT_PIN) == LOW && !interruptTriggered) {
    interruptTriggered = true; 
  }

  if (interruptTriggered) {
    delay(50); // Debounce
    
    Wire.requestFrom(PCF_ADDRESS, 1);
    if (Wire.available()) {
      uint8_t portState = Wire.read();
      
      bool isPressed = !(portState & (1 << BUTTON_PIN));

      if (isPressed) {
        Serial.println("Button PRESSED!");
        triggerAlert();
      } else {
        Serial.println("Button released.");
      }
    }
    
    interruptTriggered = false;
  }
}