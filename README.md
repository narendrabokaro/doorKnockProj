# Door Knock Notification System (ESP8266 + WhatsApp)

A smart, IoT-based door knock detection system that uses an ESP8266 and a vibration sensor to send instant WhatsApp notifications when someone knocks on your door. This project is designed to be highly responsive while filtering out false triggers from door slams or environmental noise.

## 🚀 Key Features

* **Instant WhatsApp Alerts:** Integrated with the Whatabot API to send notifications directly to your phone.
* **Hardware-Interrupt Driven:** Uses ESP8266 hardware interrupts to capture micro-second vibration pulses that standard polling might miss.
* **Intelligent Filtering:** * **Burst Detection:** Requires a specific "burst" of pulses to verify a human knock rather than a single shock.
    * **Slam Prevention:** Integrated with a magnetic reed switch to "deafen" the sensor for 2 seconds after the door is closed hard.
* **Non-Blocking Logic:** The system remains fully responsive and continues monitoring sensors even while processing network requests or managing LED signals.
* **Visual Feedback:** Dedicated LED patterns for WiFi connecting, successful notification, and system heartbeat.

## 🛠️ Hardware Requirements

* **MCU:** ESP8266 (NodeMCU or Wemos D1 Mini)
* **Vibration Sensor:** SW-18010P (Spring-based vibration switch)
* **Door State Sensor:** Magnetic Reed Switch (for slam prevention)
* **Power:** 5V Wall Plug (for 24/7 reliability)
* **Indicator:** Built-in or external LED (D0/GPIO 16)

## 🔌 Pin Mapping

| Component | ESP8266 Pin | GPIO | Function |
| :--- | :--- | :--- | :--- |
| **Vibration Sensor** | D5 | 14 | Pulse Input (Interrupt) |
| **Reed Switch** | D6 | 12 | Door State (Slam-guard) |
| **Status LED** | D0 | 16 | System Feedback |
| **Cam Trigger** | D7 | 13 | (Optional) Trigger for Phase 2 |
| **RF Transmitter**| D8 | 15 | (Optional) 433MHz Slave Alert |

## ⚙️ Configuration

Before uploading the code, update the following variables in the `.ino` file with your credentials:

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const String apiKey = "YOUR_WHATABOT_API_KEY";
const String phoneNumber = "YOUR_PHONE_NUMBER"; // No '+' sign
```

## 📝 How it Works

1.  **Sensing:** The system uses `attachInterrupt` on `FALLING` edges to catch the tiny flickers of the vibration spring.
2.  **Validation:** It looks for at least 2 pulses within a 500ms window to confirm a valid "knock signature."
3.  **Slam Guard:** If the reed switch detects the door just closed, it ignores all vibrations for 2 seconds to prevent the "slam" from triggering a false alert.
4.  **Notification:** Upon a valid trigger, it uses `BearSSL::WiFiClientSecure` to securely POST the notification to Whatabot.

## 📅 Project Roadmap

* [x] **Phase 1:** Core knock detection and WhatsApp integration.
* [ ] **Phase 2 (Master-Slave):** 433MHz radio broadcast to an ATtiny85-powered buzzer unit in another room.
* [ ] **Phase 3 (Surveillance):** Integrating an ESP32-CAM to snap a photo and send it via WhatsApp upon detection.
