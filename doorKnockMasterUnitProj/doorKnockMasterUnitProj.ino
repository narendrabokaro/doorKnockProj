/**
 * ============================================================================
 * Project: Smart Porch Security Hub - Master Controller
 * Target Board: LOLIN(WEMOS) D1 R2 & mini (ESP8266)
 * File: Master_Porch_Controller.ino
 * ============================================================================
 * 
 * DESCRIPTION:
 * This sketch acts as the central coordinator for the smart porch security system.
 * It interfaces with a remote PCF8574 8-bit I/O expander over I2C and coordinates
 * user inputs, alert peripherals, and external camera triggers.
 *
 * Pure hardware/I2C coordinator for the Porch Security Hub. Manages the PCF8574
 * I/O expander, runs a boot diagnostic chirp/blink self-test, pulses the ESP32-CAM 
 * trigger line on doorbell events, and executes the 3x buzzer/LED pattern.
 * Wi-Fi is disabled to prevent RF transients and conserve power.
 * 
 * PIN MAPPINGS:
 * ----------------------------------------------------------------------------
 * Wemos D1 Mini Pin | Connection / Function
 * ----------------------------------------------------------------------------
 * D4 (GPIO 2)       | Onboard Blue LED (LED_BUILTIN, Active LOW)
 * D5 (GPIO 14)      | I2C SDA (to PCF8574 SDA)
 * D7 (GPIO 13)      | I2C SCL (to PCF8574 SCL)
 * D2 (GPIO 4)       | Hardware INT (to PCF8574 INT with pull-up)
 * 5V / 3V3 / GND    | Shared Power and System Ground Bus
 * 
 * PCF8574 Pin Map (Address: 0x20):
 * ----------------------------------------------------------------------------
 * P0 (Pin 4)        | Doorbell Switch (Active LOW input to GND)
 * P1 (Pin 5)        | Status / Porch Red LED (Active LOW sinking output)
 * P2 (Pin 6)        | Alert Buzzer (Active LOW sinking output)
 * P3 (Pin 7)        | ESP32-CAM Trigger Pulse (Connect to ESP32-CAM GPIO 13)
 * P4 - P7           | Unused (held HIGH as inputs / weak pull-ups)
 * ============================================================================
 */

/**
 * ============================================================================
 * Project: Smart Porch Security Hub - Master Controller
 * Target Board: LOLIN(WEMOS) D1 R2 & mini (ESP8266)
 * File: Master_Porch_Controller.ino
 * ============================================================================
 */

#include <Wire.h>
#include <ESP8266WiFi.h>

#define PCF_ADDRESS     0x20
#define SDA_PIN         D5  // GPIO 14
#define SCL_PIN         D7  // GPIO 13
#define INT_PIN         D2  // GPIO 4

#define BUTTON_PIN      0   // P0: Doorbell Pushbutton
#define LED_PIN         1   // P1: External Red Porch LED
#define BUZZER_PIN      2   // P2: Piezo Buzzer
#define CAM_TRIGGER_PIN 3   // P3: ESP32-CAM GPIO 13 Trigger

#define ONBOARD_LED     LED_BUILTIN // GPIO 2 (Active LOW on Wemos D1 Mini)

// Doorbell Cooldown Timing
const unsigned long COOLDOWN_INTERVAL_MS = 3000;
unsigned long lastTriggerTime = 0;

// Non-blocking Heartbeat Timing
const unsigned long HEARTBEAT_INTERVAL_MS = 5000; // Time between blinks
const unsigned long HEARTBEAT_PULSE_MS    = 50;   // Blink duration
unsigned long lastHeartbeatTime = 0;
bool heartbeatState = false;

volatile bool interruptTriggered = false;

// 1 = Quasi-bidirectional Input / Output OFF
// 0 = Sinking Output ON
uint8_t expanderState = 0xFF; 

IRAM_ATTR void handleInterrupt() {
  interruptTriggered = true;
}

void writePcf(uint8_t data) {
  Wire.beginTransmission(PCF_ADDRESS);
  Wire.write(data);
  Wire.endTransmission();
}

void runStartupDiagnostics() {
  Serial.println("Running Startup Diagnostic Sequence...");

  // Phase 1: Rapid triple blink on onboard Wemos LED
  for (int i = 0; i < 3; i++) {
    digitalWrite(ONBOARD_LED, LOW);  // ON
    delay(70);
    digitalWrite(ONBOARD_LED, HIGH); // OFF
    delay(70);
  }

  // Phase 2: Hardware chirp via PCF8574
  expanderState &= ~(1 << LED_PIN);
  expanderState &= ~(1 << BUZZER_PIN);
  writePcf(expanderState);

  digitalWrite(ONBOARD_LED, LOW);
  delay(120);

  expanderState |= (1 << LED_PIN);
  expanderState |= (1 << BUZZER_PIN);
  writePcf(expanderState);

  digitalWrite(ONBOARD_LED, HIGH);
  Serial.println("Startup Self-Test Complete.");
}

void triggerAlert() {
  Serial.println("Doorbell Activated! Triggering ESP32-CAM and sounding alert...");

  // 1. Send Active-LOW pulse to ESP32-CAM (P3 = 0)
  expanderState &= ~(1 << CAM_TRIGGER_PIN);
  writePcf(expanderState);
  
  delay(50); // 50ms verified pulse width

  // Release camera trigger line back HIGH (P3 = 1)
  expanderState |= (1 << CAM_TRIGGER_PIN);
  writePcf(expanderState);

  // 2. 3x Chime & Strobe alert sequence (~900ms total)
  for (int i = 0; i < 3; i++) {
    expanderState &= ~(1 << LED_PIN);
    expanderState &= ~(1 << BUZZER_PIN);
    writePcf(expanderState);
    digitalWrite(ONBOARD_LED, LOW);
    
    delay(150);
    
    expanderState |= (1 << LED_PIN);
    expanderState |= (1 << BUZZER_PIN);
    writePcf(expanderState);
    digitalWrite(ONBOARD_LED, HIGH);
    
    delay(150);
  }
}

// Non-blocking Heartbeat routine
void handleHeartbeat() {
  unsigned long currentMillis = millis();

  if (!heartbeatState) {
    // Check if it's time to turn the LED ON
    if (currentMillis - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS) {
      lastHeartbeatTime = currentMillis;
      digitalWrite(ONBOARD_LED, LOW); // Active-LOW: Turn LED ON
      heartbeatState = true;
    }
  } else {
    // Check if the 50ms flash duration has elapsed
    if (currentMillis - lastHeartbeatTime >= HEARTBEAT_PULSE_MS) {
      digitalWrite(ONBOARD_LED, HIGH); // Turn LED OFF
      heartbeatState = false;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Smart Porch Security Hub: Master Controller ===");

  // Power down Wi-Fi radio completely to save power & remove RF noise
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, HIGH); // Default OFF (active LOW)

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  pinMode(INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INT_PIN), handleInterrupt, FALLING);

  writePcf(expanderState);
  runStartupDiagnostics();

  // Clear any startup interrupt states
  Wire.requestFrom(PCF_ADDRESS, 1);
  if (Wire.available()) Wire.read();

  Serial.println("System Armed & Ready. Listening on PCF8574 P0...\n");
}

void loop() {
  // Service non-blocking heartbeat LED
  handleHeartbeat();

  // Watchdog backup check in case falling edge was missed
  if (digitalRead(INT_PIN) == LOW && !interruptTriggered) {
    interruptTriggered = true; 
  }

  if (interruptTriggered) {
    delay(50); // Contact debounce settling time
    
    Wire.requestFrom(PCF_ADDRESS, 1);
    if (Wire.available()) {
      uint8_t portState = Wire.read();
      bool isPressed = !(portState & (1 << BUTTON_PIN));

      if (isPressed) {
        // Enforce 3-second lockout window
        if (millis() - lastTriggerTime >= COOLDOWN_INTERVAL_MS) {
          lastTriggerTime = millis();
          triggerAlert();
          // Reset heartbeat tracking so it doesn't immediately glitch after alert
          lastHeartbeatTime = millis();
          digitalWrite(ONBOARD_LED, HIGH);
          heartbeatState = false;
        } else {
          Serial.println("Doorbell press ignored (Cooldown window active).");
        }
      }
    }
    
    // Clear flag and flush any latched interrupts that arrived during the chime
    interruptTriggered = false;
    Wire.requestFrom(PCF_ADDRESS, 1);
    if (Wire.available()) Wire.read();
  }
}
