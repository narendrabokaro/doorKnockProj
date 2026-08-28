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