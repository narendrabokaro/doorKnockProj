/**
 * ============================================================================
 * Project: Smart Porch Security Hub - Edge Camera Capture Module
 * Target Board: AI Thinker ESP32-CAM (OV2640)
 * File: ESP32_CAM_Trigger_Capture.ino
 * ============================================================================
 * 
 * DESCRIPTION:
 * This sketch runs on the AI Thinker ESP32-CAM to provide on-demand, trigger-based
 * image acquisition for the smart porch security system. It stays idle in a low-
 * overhead monitoring state until an external trigger pulse is received from the
 * Master controller (via the PCF8574 I/O expander), then activates the high-power
 * onboard flash LED and captures an SVGA/VGA frame buffer into internal memory.
 * 
 * CORE FEATURES:
 * 1. Hardware Interrupt Triggering:
 *    - Uses a FALLING hardware interrupt on GPIO 13 to detect trigger pulses 
 *      instantly without polling loops.
 * 2. Synchronized Scene Illumination:
 *    - Toggles the high-intensity onboard flash LED (GPIO 4) directly around
 *      the camera exposure window to illuminate low-light porch scenes.
 * 3. Frame Buffer Memory Management:
 *    - Configures and queries the OV2640 sensor for compressed JPEG frames.
 *    - Dynamically acquires (`esp_camera_fb_get`) and releases 
 *      (`esp_camera_fb_return`) frame buffers to avoid PSRAM/heap memory leaks.
 * 4. Modular Output Pipeline:
 *    - Provides a verified, standalone capture foundation ready for direct
 *      piping to cloud messaging APIs (e.g., WhatsApp, Telegram) or local storage.
 * 
 * PIN MAPPINGS:
 * ----------------------------------------------------------------------------
 * ESP32-CAM Pin     | Connection / Function
 * ----------------------------------------------------------------------------
 * GPIO 13 (IO13)    | Trigger Input (Connect to PCF8574 Pin P3 / Active-LOW pulse)
 * GPIO 4  (IO4)     | Onboard High-Power White Flash LED (Active-HIGH)
 * GPIO 33 (IO33)    | Onboard Small Red Status Indicator (Active-LOW, optional)
 * 5V / GND          | 5V Power Supply (min 2A recommended) & Common System GND
 * U0T / U0R         | Serial UART (TX/RX) for flashing & diagnostic logging
 * 
 * OV2640 Internal Bus (AI Thinker Pinout):
 * - Data Pins: Y2 (GPIO 5), Y3 (GPIO 18), Y4 (GPIO 19), Y5 (GPIO 21),
 *              Y6 (GPIO 36), Y7 (GPIO 39), Y8 (GPIO 34), Y9 (GPIO 35)
 * - Clock / Sync: XCLK (GPIO 0), PCLK (GPIO 22), VSYNC (GPIO 25), HREF (GPIO 23)
 * - Control / SCCB: SIOD (GPIO 26), SIOC (GPIO 27), PWDN (GPIO 32), RESET (-1)
 * 
 * ARDUINO IDE BOARD CONFIGURATION:
 * ----------------------------------------------------------------------------
 * Board:              AI Thinker ESP32-CAM
 * CPU Frequency:      240MHz (WiFi/BT)
 * Flash Frequency:    80MHz
 * Flash Mode:         QIO
 * Partition Scheme:   Huge App (3MB No OTA / 1MB SPIFFS)
 * PSRAM:              Enabled
 * Upload Speed:       115200 (or 921600 if adapter supports it)
 * 
 * HOW TO USE:
 * 1. Flashing Instructions:
 *    - Bridge GPIO 0 to GND using a jumper.
 *    - Connect FTDI/programmer: TX -> U0R, RX -> U0T, GND -> GND, 5V -> 5V.
 *    - Press the RST button on the underside, then hit Upload in Arduino IDE.
 * 2. Normal Operation:
 *    - Disconnect GPIO 0 from GND.
 *    - Press RST once to reboot into application mode.
 * 3. Operation & Verification:
 *    - Open Serial Monitor at 115200 baud.
 *    - When the doorbell switch is pressed on the Master hub, PCF8574 P3 pulses
 *      LOW, the flash LED strobes, and the captured frame metrics log to Serial.
 * ============================================================================
*/

/**
 * ============================================================================
 * Project: Smart Porch Security Hub - Edge Vision Node
 * Board: AI-Thinker ESP32-CAM (or ESP32 Dev Module with PSRAM: Enabled)
 * Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
 * ============================================================================
 * 
 * Hardware Connections:
 * - GPIO 13 (IO13) : Hardware Trigger Input from Master PCF8574 P3 (Active-LOW pulse)
 * - GPIO 4  (IO4)  : Onboard High-Power White Flash LED (Clean DC control)
 * - 5V / GND       : Stable 5V (>= 2A) & Shared Ground with Master Node
 */


#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ================= USER CONFIGURATION =================
const char* ssid     = "<your wifi>";
const char* password = "<your wifi password>";

const char* botToken = "<your API Key>";
const char* chatID   = "<your password>";
// ======================================================

// AI-THINKER ESP32-CAM Pin Definitions
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Hardware Interfaces
#define TRIGGER_PIN       13  // From PCF8574 P3
#define FLASH_LED_PIN      4  // Onboard Flash LED

volatile bool captureTriggered = false;

// Interrupt Service Routine for Doorbell Trigger
void IRAM_ATTR triggerISR() {
  captureTriggered = true;
}

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_VGA;  // 640x480
  config.jpeg_quality = 12;
  config.fb_count     = 2;              // Double buffering via PSRAM
  config.grab_mode    = CAMERA_GRAB_LATEST;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed!");
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_agc_gain(s, 0);
    s->set_raw_gma(s, 1);
    s->set_lenc(s, 1);
  }
  Serial.println("Camera Initialized & Configured with PSRAM!");
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("\nConnecting to Wi-Fi: %s", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.printf("\nWi-Fi Connection Failed! Status: %d\n", WiFi.status());
  }
}

// Single HTTPS Request: Streams raw JPEG with alert caption
bool sendTelegramPhoto(camera_fb_t* fb) {
  if (WiFi.status() != WL_CONNECTED || !fb) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12000);

  Serial.println("Connecting to api.telegram.org...");
  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("Telegram connection failed!");
    return false;
  }

  String boundary = "PorchCamBoundary7MA4YWxkTrZu0gW";
  String head = "--" + boundary + "\r\n"
              + "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
              + String(chatID) + "\r\n"
              + "--" + boundary + "\r\n"
              + "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
              + " Porch Alert: Doorbell button pressed!\r\n"
              + "--" + boundary + "\r\n"
              + "Content-Disposition: form-data; name=\"photo\"; filename=\"visitor.jpg\"\r\n"
              + "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";
  size_t totalLen = head.length() + fb->len + tail.length();

  client.println("POST /bot" + String(botToken) + "/sendPhoto HTTP/1.1");
  client.println("Host: api.telegram.org");
  client.println("Content-Length: " + String(totalLen));
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Connection: close");
  client.println();

  // Send multipart head
  client.print(head);

  // Stream raw JPEG bytes in 1024-byte chunks
  uint8_t *buf = fb->buf;
  size_t len = fb->len;
  size_t chunkSize = 1024;
  for (size_t i = 0; i < len; i += chunkSize) {
    if (i + chunkSize < len) {
      client.write(buf + i, chunkSize);
    } else {
      client.write(buf + i, len - i);
    }
  }

  // Send multipart tail
  client.print(tail);

  // Await Telegram response
  unsigned long timeout = millis();
  bool success = false;
  while (client.connected() || client.available()) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (line.indexOf("200 OK") > 0) {
        success = true;
      }
    }
    if (millis() - timeout > 6000) break;
  }
  client.stop();

  if (success) {
    Serial.println("Photo & caption delivered successfully!");
  } else {
    Serial.println("Warning: Upload finished without confirming HTTP 200.");
  }
  return success;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Smart Porch Security Hub: Fast Edge Vision Node ===");

  // Verify PSRAM state
  if (psramFound()) {
    Serial.printf("PSRAM Enabled! Total Free: %d bytes\n", ESP.getFreePsram());
  } else {
    Serial.println("Warning: PSRAM flag is not active! Enable in Tools menu.");
  }

  // Configure clean DC Flash pin
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  // Initialize Camera Hardware
  initCamera();

  // Connect to Wi-Fi
  connectWiFi();

  // Sensor warm-up: discard initial startup frames
  Serial.println("Stabilizing sensor registers...");
  for (int i = 0; i < 4; i++) {
    camera_fb_t * dummy = esp_camera_fb_get();
    if (dummy) {
      esp_camera_fb_return(dummy);
    }
    delay(80);
  }
  Serial.println("Sensor ready!");

  // Hardware interrupt trigger from PCF8574 P3
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TRIGGER_PIN), triggerISR, FALLING);

  Serial.println("System Armed! Waiting for Doorbell trigger on GPIO 13...\n");
}

void loop() {
  if (captureTriggered) {
    captureTriggered = false;
    Serial.println("\nINSTANT DOORBELL TRIGGER DETECTED!");

    // ==========================================================
    // PHASE 1: IMMEDIATE OPTICAL CAPTURE (~150ms total)
    // ==========================================================
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(60); // Exposure lock settling time

    // Flush stale unlit frame
    camera_fb_t * stale = esp_camera_fb_get();
    if (stale) {
      esp_camera_fb_return(stale);
    }

    // Capture the illuminated visitor
    camera_fb_t * fb = esp_camera_fb_get();

    // Turn flash OFF immediately
    digitalWrite(FLASH_LED_PIN, LOW);

    // ==========================================================
    // PHASE 2: SECURE CLOUD DISPATCH
    // ==========================================================
    if (fb) {
      Serial.printf("Frame secured: %u bytes (%dx%d). Uploading with caption...\n", 
                    fb->len, fb->width, fb->height);

      // Verify network link
      if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
      }

      // Upload image + caption directly
      sendTelegramPhoto(fb);

      // Free buffer back to PSRAM pool
      esp_camera_fb_return(fb);
    } else {
      Serial.println("Camera capture failed!");
    }

    Serial.println("Complete. Re-armed and standing by.\n");
  }
}
