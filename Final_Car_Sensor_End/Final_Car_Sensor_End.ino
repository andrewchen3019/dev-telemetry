// HC-SR04 + CAN (Initiator) - diagnostic & robust twai transmit

#include <Arduino.h>
#include "driver/twai.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

// --- HC-SR04 pins ---
const int trigPin = 21;
const int echoPin = 19;

// --- CAN/TWAI pins ---
const gpio_num_t CAN_TX_PIN = GPIO_NUM_5; // TXD -> ESP GPIO5
const gpio_num_t CAN_RX_PIN = GPIO_NUM_4; // RXD -> ESP GPIO4

const uint32_t ID_TX = 0x100; // Initiator -> Receiver

// Timing
const unsigned long SEND_INTERVAL_MS = 1000; // sends every 200 ms 
const TickType_t TWAI_TX_TIMEOUT = pdMS_TO_TICKS(100);
unsigned long lastSendMs = 0;

// --- helpers for TWAI (from your original Initiator_CAN) ---
bool twaiInit(twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS()) {
  twai_general_config_t gconf = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t tconf = timing;
  twai_filter_config_t fconf = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t r = twai_driver_install(&gconf, &tconf, &fconf);
  if (r != ESP_OK) {
    Serial.printf("twai_driver_install failed: %d\n", (int)r);
    return false;
  }
  r = twai_start();
  if (r != ESP_OK) {
    Serial.printf("twai_start failed: %d\n", (int)r);
    twai_driver_uninstall();
    return false;
  }
  return true;
}

// 
bool twaiSendUint32(uint32_t id, uint32_t value, int max_retries = 3) {
  // prepare bytes (big-endian)
  uint8_t b0 = (uint8_t)((value >> 24) & 0xFF);
  uint8_t b1 = (uint8_t)((value >> 16) & 0xFF);
  uint8_t b2 = (uint8_t)((value >> 8) & 0xFF);
  uint8_t b3 = (uint8_t)(value & 0xFF);

  Serial.printf("TX attempt: ID=0x%03X value=%lu -> bytes: %u(0x%02X) %u(0x%02X) %u(0x%02X) %u(0x%02X)\n",
                id, (unsigned long)value,
                b0, b0, b1, b1, b2, b2, b3, b3);

  for (int attempt = 1; attempt <= max_retries; ++attempt) {
    // zero-initialize message to avoid stray/uninitialized fields (apparently necessary)
    twai_message_t msg;
    memset(&msg, 0, sizeof(msg));

    msg.identifier = id;
    msg.extd = 0;
    msg.rtr = 0;
    msg.data_length_code = 4;
    msg.data[0] = b0;
    msg.data[1] = b1;
    msg.data[2] = b2;
    msg.data[3] = b3;

    esp_err_t r = twai_transmit(&msg, TWAI_TX_TIMEOUT);
    if (r == ESP_OK) {
      Serial.printf("twai_transmit OK (attempt %d)\n", attempt);
      return true;
    } else {
      Serial.printf("twai_transmit FAIL (attempt %d) -> err=%d\n", attempt, (int)r);
      // small backoff before retrying
      vTaskDelay(pdMS_TO_TICKS(10 * attempt));
    }
  }
  return false;
}

// --- ultrasonic measurement ---
uint32_t readUltrasonic_mm() {
  // Trigger pulse
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // measure echo. pulseIn returns microseconds
  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL); // timeout 30 ms (avoid blocking forever)
  if (duration == 0) {
    // no echo (out of range / timeout). Represent as 0xFFFFFFFF (max) to indicate no reading.
    return 0xFFFFFFFFUL;
  }

  // distance in centimeters: (duration us * 0.0343) / 2
  // convert to millimeters: cm * 10 => (duration * 0.0343 / 2) * 10 = duration * 0.1715
  float distance_cm = (duration * 0.0343f) / 2.0f;
  uint32_t distance_mm = (uint32_t)(distance_cm * 10.0f + 0.5f); // round to nearest mm
  return distance_mm;
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println();
  Serial.println("Initiator: HC-SR04 -> CAN sender (diagnostic) starting...");

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  if (!twaiInit()) {
    Serial.println("TWAI init failed. Check wiring and 3.3V VCC.");
  } else {
    Serial.println("TWAI init OK (500 kbps).");
  }

  lastSendMs = millis();
  Serial.printf("HC-SR04 trig=%d echo=%d CAN TX pin: %d RX pin: %d\n", trigPin, echoPin, CAN_TX_PIN, CAN_RX_PIN);
  Serial.println("Sending distance (mm) under CAN ID 0x100 every 200 ms. 0xFFFFFFFF => timeout/no-echo.");
}

void loop() {
  unsigned long now = millis();
  if ((now - lastSendMs) >= SEND_INTERVAL_MS) {
    lastSendMs = now;

    uint32_t dist_mm = readUltrasonic_mm();
    if (dist_mm == 0xFFFFFFFFUL) {
      Serial.println("Ultrasonic: timeout / out of range.");
    } else {
      Serial.printf("Ultrasonic: %lu mm\n", (unsigned long)dist_mm);
    }

    bool ok = twaiSendUint32(ID_TX, dist_mm);
    Serial.println(ok ? "CAN TX OK" : "CAN TX FAIL");
  }

  // optional: small delay to yield CPU
  delay(5);
}
