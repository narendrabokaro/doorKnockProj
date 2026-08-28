/**
 * ============================================================================
 * Project: Smart Porch Security Hub - Master Controller
 * Target Board: Wemos D1 Mini (ESP8266)
 * File: Master_Porch_Controller.ino
 * ============================================================================
 * 
 * DESCRIPTION:
 * This sketch acts as the central coordinator for the smart porch security system.
 * It interfaces with a remote PCF8574 8-bit I/O expander over I2C and coordinates
 * user inputs, alert peripherals, and external camera triggers.
 * 
 * CORE FEATURES:
 * 1. Hardware Interrupt Driven Input:
 *    - Monitors the PCF8574 INT line (D2) via a FALLING interrupt to detect
 *      button presses instantly without continuous polling overhead.
 * 2. Active-Low Sinking Output Control:
 *    - Safely switches peripheral pins by clearing register bits (0 = ON, 1 = OFF),
 *      leveraging the PCF8574's high current sinking capability.
 * 3. Integrated Security Alert Sequence:
 *    - On doorbell press, pulses the external ESP32-CAM trigger line (P3).
 *    - Synchronously strobes the porch indicator LED (P1) and sounds the indoor 
 *      buzzer (P2) three times in a non-blocking sequence.
 * 4. Watchdog & False-Trigger Clearance:
 *    - Performs startup and post-event dummy reads to clear pending interrupt states 
 *      and prevent lockups.
 * 
 * PIN MAPPINGS:
 * ----------------------------------------------------------------------------
 * Wemos D1 Mini Pin | Connection / Function
 * ----------------------------------------------------------------------------
 * D5 (GPIO 14)      | I2C SDA (to PCF8574 SDA)
 * D7 (GPIO 13)      | I2C SCL (to PCF8574 SCL)
 * D2 (GPIO 4)       | Hardware INT (to PCF8574 INT with pull-up)
 * 5V / 3V3 / GND    | Power and common reference bus
 * 
 * PCF8574 Pin Map (Address: 0x20):
 * ----------------------------------------------------------------------------
 * P0 (Pin 4)        | Doorbell Switch (Active LOW input to GND)
 * P1 (Pin 5)        | Status / Porch LED (Active LOW sinking output)
 * P2 (Pin 6)        | Alert Buzzer (Active LOW sinking output)
 * P3 (Pin 7)        | ESP32-CAM Trigger Pulse (Connect to ESP32-CAM GPIO 13)
 * P4 - P7           | Unused (held HIGH as inputs)
 * 
 * HOW TO USE:
 * 1. Hardware Setup:
 *    - Ensure common GND between Wemos D1 Mini, PCF8574, and ESP32-CAM.
 *    - Connect 4.7kΩ–10kΩ pull-up resistors on SDA, SCL, and INT lines if not 
 *      present on breakout modules.
 *    - Set PCF8574 address pins (A0, A1, A2) to GND for default 0x20 address.
 * 2. Configuration:
 *    - Verify pin definitions match your breadboard layout.
 * 3. Flashing:
 *    - Select 'LOLIN(WEMOS) D1 R2 & mini' in Arduino IDE.
 *    - Compile and upload via standard USB port (115200 baud).
 * 4. Operation:
 *    - Pressing the switch on P0 triggers the 3x LED/Buzzer alert pattern and 
 *      sends a 50ms LOW trigger pulse to ESP32-CAM GPIO 13 to initiate capture.
 * ============================================================================
 */
 
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