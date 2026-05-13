#include <SoftwareSerial.h>
#include <RCSwitch.h>

// Pins
const int rxPin = 4; // Not used but needed for constructor
const int txPin = 3; // Physical Pin 2 -> Nano D2
const int rfPin = 2; // Physical Pin 7 -> RF Data
const int bzrPin = 1; // Physical Pin 6 -> Buzzer

SoftwareSerial mySerial(-1, txPin); // TX only to save space
RCSwitch mySwitch = RCSwitch();

void setup() {
  mySerial.begin(9600);
  pinMode(bzrPin, OUTPUT);

  mySerial.println("--- ATtiny85 Debug Boot ---");
  
  // Test Beep
  digitalWrite(bzrPin, HIGH);
  delay(200);
  digitalWrite(bzrPin, LOW);

  // Initialize Receiver on INT0 (PB2)
  mySwitch.enableReceive(0); 
  mySerial.println("Receiver Armed on PB2...");
}

void loop() {
  if (mySwitch.available()) {
    long value = mySwitch.getReceivedValue();
    mySerial.print("Signal Caught! Value: ");
    mySerial.println(value);

    if (value == 1001) {
      mySerial.println("MATCH! Beeping now.");
      digitalWrite(bzrPin, HIGH);
      delay(500);
      digitalWrite(bzrPin, LOW);
    }
    mySwitch.resetAvailable();
  }
}