#include <RCSwitch.h>
#include <ESP8266WiFi.h>

RCSwitch mySwitch = RCSwitch();

// Pin Definitions
const int buzzerPin = D5;       // Connected to the Base of your P2N2222A transistor (with 1k resistor)
const int onboardLED = LED_BUILTIN; // Wemos D1 Mini onboard blue LED (Note: LOW = ON, HIGH = OFF)

// System State Variables
bool isSleeping = false;

// Non-Blocking Heartbeat Variables
unsigned long previousMillis = 0;
const long heartbeatInterval = 10000; // Blink every 10 second

void setup() {
  Serial.begin(115200);
  
  // 2. FORCE THE INTERNAL WI-FI RADIO TO SHUT DOWN COMPLETELY
  WiFi.disconnect();        // Break any automatic connections
  WiFi.mode(WIFI_OFF);      // Turn off the internal 2.4GHz radio transceiver
  WiFi.forceSleepBegin();   // Put the Wi-Fi modem into a deep electronic sleep
  delay(1);                 // Short pause to let the radio power rail collapse safely
  
  Serial.println("Internal Wi-Fi Radio Powered Down permanently.");

  // Initialize Pins
  pinMode(buzzerPin, OUTPUT);
  pinMode(onboardLED, OUTPUT);
  
  digitalWrite(buzzerPin, LOW);     
  digitalWrite(onboardLED, HIGH);   
  
  // Your 433MHz external radio remains fully operational on D2!
  mySwitch.enableReceive(digitalPinToInterrupt(D2));
  Serial.println("Slave Unit Ready. Monitoring Airwaves via 433MHz only...");

  // Quick startup beep
  digitalWrite(buzzerPin, HIGH);
  delay(500);
  digitalWrite(buzzerPin, LOW);
  delay(500);
}

void loop() {
  // 1. Handle the Heartbeat LED (Only if the system is awake)
  unsigned long currentMillis = millis();
  
  if (!isSleeping) {
    if (currentMillis - previousMillis >= heartbeatInterval) {
      previousMillis = currentMillis;
      digitalWrite(onboardLED, LOW);
      delay(100);
      digitalWrite(onboardLED, HIGH);
    }
  } else {
    // Force LED off during sleep mode
    digitalWrite(onboardLED, HIGH); 
  }

  // 2. Listen for incoming 433MHz RF Signals
  if (mySwitch.available()) {
    long receivedValue = mySwitch.getReceivedValue();
    
    if (receivedValue != 0) {
      Serial.print("RF Code Received: ");
      Serial.println(receivedValue);
      
      // Process the received RF code
      handleRFCode(receivedValue);
    }
    
    mySwitch.resetAvailable(); // Clear the flag for the next packet
  }
}

// Helper function to handle the system logic based on codes
void handleRFCode(long code) {
  switch (code) {
    case 9999:
      // MASTER SAYS BEDTIME
      if (!isSleeping) {
        isSleeping = true;
        Serial.println("System Muted. Entering Sleep Mode.");
        playStateChime(false); // Play descending "Goodnight" melody
      }
      break;
      
    case 1111:
      // MASTER SAYS WAKE UP
      if (isSleeping) {
        isSleeping = false;
        Serial.println("System Active. Entering Daytime Mode.");
        playStateChime(true); // Play ascending "Good Morning" melody
      }
      break;
      
    case 1001:
      // VISITOR IS AT THE DOOR
      if (!isSleeping) {
        Serial.println("Secret Knock Verified! Triggering Alert.");
        triggerBuzzerAlert();
      } else {
        Serial.println("Secret Knock detected, but system is muted for sleep.");
      }
      break;
      
    default:
      Serial.println("Unknown/Foreign RF code ignored.");
      break;
  }
}

// Function to trigger the standard door alert via the P2N2222A transistor
void triggerBuzzerAlert() {
  // A three-beep pattern for the door alert
  for (int i = 0; i < 3; i++) {
    digitalWrite(buzzerPin, HIGH);
    delay(500);
    digitalWrite(buzzerPin, LOW);
    delay(500);
  }
}

// Function to play unique chimes for state changes
void playStateChime(bool wakingUp) {
  if (wakingUp) {
    // "Good Morning" Ascending Tones
    for (int duration = 50; duration <= 250; duration += 50) {
      digitalWrite(buzzerPin, HIGH);
      delay(duration);
      digitalWrite(buzzerPin, LOW);
      delay(50);
    }
  } else {
    // "Goodnight" Descending Tones
    for (int duration = 250; duration >= 50; duration -= 50) {
      digitalWrite(buzzerPin, HIGH);
      delay(duration);
      digitalWrite(buzzerPin, LOW);
      delay(50);
    }
  }
}
