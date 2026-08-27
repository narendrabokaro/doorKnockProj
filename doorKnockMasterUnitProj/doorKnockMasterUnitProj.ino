#include <Wire.h>

// --- PIN DEFINITIONS (Wemos D1 Mini) ---
#define MASTER_SDA    13  // D7 pin on Wemos D1 Mini
#define MASTER_SCL    14  // D5 pin on Wemos D1 Mini
#define INT_PIN       12  // D6 pin on Wemos D1 Mini (Interrupt line)
#define BUZZER_PIN    4   // D2 pin on Wemos D1 Mini

// --- PCF8574 CONFIGURATION ---
#define PCF_I2C_ADDR  0x20 // Change to 0x38 if your PCF8574AT chip uses 0x38

// PCF8574 Pin Map
#define PCF_P0_DOORBELL  0
#define PCF_P1_PIR       1
#define PCF_P2_LED       2
#define PCF_P3_CAM       3

// --- TIMING CONSTANTS ---
#define LOITERING_THRESHOLD 8000 // 8 seconds of persistent motion triggers camera
#define CAM_PULSE_DURATION  50   // 50ms LOW pulse on P3 to trigger camera

// Global State
uint8_t pcfOutputState = 0xFF; // All pins HIGH by default
unsigned long motionStartTime = 0;
bool isLoiteringTriggered = false;

// --- I2C HELPER FUNCTIONS ---
void writePCF(uint8_t data) {
  pcfOutputState = data;
  Wire.beginTransmission(PCF_I2C_ADDR);
  Wire.write(pcfOutputState);
  Wire.endTransmission();
}

uint8_t readPCF() {
  Wire.requestFrom(PCF_I2C_ADDR, 1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0xFF;
}

void triggerCamera() {
  Serial.println("LOITERING DETECTED! Pulse P3 LOW -> ESP32-CAM Trigger");
  
  // Set P3 LOW while preserving other output pin states
  uint8_t pulseLow = pcfOutputState & ~(1 << PCF_P3_CAM);
  writePCF(pulseLow);
  delay(CAM_PULSE_DURATION);
  
  // Return P3 to HIGH
  uint8_t pulseHigh = pcfOutputState | (1 << PCF_P3_CAM);
  writePCF(pulseHigh);
}

void playChime() {
  tone(BUZZER_PIN, 1046, 150); // High note (C6)
  delay(180);
  tone(BUZZER_PIN, 784, 300);  // Low note (G5)
  delay(320);
  noTone(BUZZER_PIN);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Master Porch Security Control Hub Initializing ===");

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(INT_PIN, INPUT_PULLUP);

  Wire.begin(MASTER_SDA, MASTER_SCL);

  // Initialize PCF8574 outputs HIGH (LED off, CAM line HIGH)
  writePCF(0xFF);

  Serial.println("Hardware Ready. Monitoring Porch inputs...");
}

void loop() {
  uint8_t inputs = readPCF();

  bool doorbellPressed = !(inputs & (1 << PCF_P0_DOORBELL)); // Active LOW
  bool motionDetected  = (inputs & (1 << PCF_P1_PIR));        // Active HIGH

  // 1. DOORBELL EVENT HANDLING
  if (doorbellPressed) {
    Serial.println("Doorbell Pressed!");
    
    // Turn ON LED (P2 LOW) and trigger immediate camera capture
    writePCF(pcfOutputState & ~(1 << PCF_P2_LED)); 
    playChime();
    triggerCamera();
    
    // Turn OFF LED
    writePCF(pcfOutputState | (1 << PCF_P2_LED));
    delay(1000); // Debounce delay
  }

  // 2. PIR LOITERING DETECTION LOGIC
  if (motionDetected) {
    if (motionStartTime == 0) {
      motionStartTime = millis();
      Serial.println("Motion detected. Starting loiter timer...");
    } else if ((millis() - motionStartTime >= LOITERING_THRESHOLD) && !isLoiteringTriggered) {
      isLoiteringTriggered = true;
      
      // Blink LED and trigger camera
      for(int i=0; i<3; i++) {
        writePCF(pcfOutputState & ~(1 << PCF_P2_LED));
        delay(100);
        writePCF(pcfOutputState | (1 << PCF_P2_LED));
        delay(100);
      }
      triggerCamera();
    }
  } else {
    // Reset motion timer when subject walks away
    if (motionStartTime != 0) {
      Serial.println("Clear. Motion stopped.");
    }
    motionStartTime = 0;
    isLoiteringTriggered = false;
  }

  delay(50); // Polling loop speed
}