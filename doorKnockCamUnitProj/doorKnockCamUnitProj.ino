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

#include "esp_camera.h"
#include "Arduino.h"

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
#define TRIGGER_PIN       13  // Connect to PCF8574 P3
#define FLASH_LED_PIN      4  // Onboard bright white LED

volatile bool captureRequested = false;

void IRAM_ATTR triggerISR() {
  captureRequested = true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32-CAM Ready for Trigger ---");

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

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
  config.frame_size   = FRAMESIZE_VGA;  // 640x480 (optimal for messaging gateways)
  config.jpeg_quality = 12;
  config.fb_count     = 1;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed!");
    return;
  }
  Serial.println("Camera Initialized!");

  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TRIGGER_PIN), triggerISR, FALLING);
}

void loop() {
  if (captureRequested) {
    captureRequested = false;
    Serial.println("\nTrigger received! Capturing photo...");

    // Turn ON flash LED
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(40);

    // Capture frame buffer
    camera_fb_t * fb = esp_camera_fb_get();

    // Turn OFF flash LED
    digitalWrite(FLASH_LED_PIN, LOW);

    if (!fb) {
      Serial.println("Camera capture failed!");
      return;
    }

    Serial.printf("Photo Captured! Size: %u bytes (%dx%d)\n", fb->len, fb->width, fb->height);

    // Return the frame buffer to free memory
    esp_camera_fb_return(fb);
  }
}