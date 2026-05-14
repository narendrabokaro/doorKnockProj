// Based on Wemos D1 Mini
#include <RCSwitch.h>

RCSwitch mySwitch = RCSwitch();
const int buzzerPin = 14; // D5 on Wemos D1 Mini

void setup() {
  Serial.begin(115200);
  pinMode(buzzerPin, OUTPUT);

  // Receiver on D2 (Interrupt for GPIO 4)
  mySwitch.enableReceive(digitalPinToInterrupt(4)); 
  
  Serial.println("--- Wemos Slave Unit Online ---");
  
  // Quick startup beep
  digitalWrite(buzzerPin, HIGH);
  delay(100);
  digitalWrite(buzzerPin, LOW);
}

void loop() {
  if (mySwitch.available()) {
    long value = mySwitch.getReceivedValue();
    
    if (value == 1001) {
      Serial.println("Signal Received: 1001 - BEEPING!");
      
      // Triple-beep pattern
      for(int i=0; i<3; i++) {
        digitalWrite(buzzerPin, HIGH);
        delay(200);
        digitalWrite(buzzerPin, LOW);
        delay(100);
      }
    } else {
      Serial.print("Unknown Signal Caught: ");
      Serial.println(value);
    }
    
    mySwitch.resetAvailable();
  }
}
