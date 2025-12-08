// can_to_heltec_lora_bridge_decodes_32bit.ino
// Simplified CAN -> LoRa bridge. Forwards CAN frames as binary packets.
// Listens for LoRa CMD_RELAY commands only and toggles an attached relay.
// Updated to decode 32-bit distance values (and fallback to 16-bit).

#include <Arduino.h>
#include "driver/twai.h"
#include "heltec.h"
#include "LoRaWan_APP.h"

// ---------- CONFIG ----------
#define RF_FREQUENCY 915000000UL

#define BRIDGE_SRC_ID    0x0A   // bridge device ID on LoRa
#define BROADCAST_DST    0xFF

const uint8_t CMD_RELAY       = 0x10;
const uint8_t CMD_ACK         = 0x20;
const uint8_t CMD_CAN_FORWARD = 0x30;

const gpio_num_t CAN_TX_PIN = GPIO_NUM_5;
const gpio_num_t CAN_RX_PIN = GPIO_NUM_4;

const int RELAY_PIN = 26;                 // GPIO controlling relay
const bool RELAY_ACTIVE_HIGH = true;      // true = HIGH energizes relay

const TickType_t TWAI_RX_TIMEOUT_TICKS = pdMS_TO_TICKS(100); // CAN receive timeout
static RadioEvents_t RadioEvents;
volatile bool lora_idle = true;

// ---------- Forward decls ----------
void OnTxDone(void);
void OnTxTimeout(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void OnRxTimeout(void);
void init_can();
bool init_heltec_radio();
void printCANFrame(const twai_message_t &rx);
void radioForwardCAN_asBinary(const twai_message_t &rx);
void handleRelayCommandFromPayload(uint8_t src, uint8_t dst, uint8_t *payload, uint8_t len);
void sendAck(uint8_t to_src);
uint32_t decodeDistanceFromBytes(const uint8_t *data, uint8_t dlc);

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\nSimplified CAN->LoRa bridge starting (32-bit distance aware)...");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // default OFF

  init_can();
  init_heltec_radio();

  Serial.printf("Bridge SRC_ID=0x%02X, listening for LoRa CMD_RELAY (DST=0x%02X or BROADCAST)\n",
                BRIDGE_SRC_ID, BRIDGE_SRC_ID);
}

// ---------- CAN init ----------
void init_can() {
  Serial.printf("TWAI init -> TX pin: %d, RX pin: %d\n", CAN_TX_PIN, CAN_RX_PIN);
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t r = twai_driver_install(&g_config, &t_config, &f_config);
  if (r != ESP_OK) {
    Serial.printf("ERROR: twai_driver_install failed: %d\n", (int)r);
    while (true) delay(1000);
  }
  r = twai_start();
  if (r != ESP_OK) {
    Serial.printf("ERROR: twai_start failed: %d\n", (int)r);
    while (true) delay(1000);
  }
  Serial.println("CAN initialized (TWAI @ 500 kbps)");
}

// ---------- LoRa init ----------
bool init_heltec_radio() {
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  RadioEvents.TxDone    = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxDone    = OnRxDone;
  RadioEvents.RxTimeout = OnRxTimeout;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);

  Radio.SetTxConfig(MODEM_LORA, 5, 0, 0, 7, 1, 8, false, true, 0, 0, false, 3000);
  Radio.SetRxConfig(MODEM_LORA, 0, 7, 1, 0, 8, 0, false, 0, true, 0, 0, false, true);

  lora_idle = false;
  Radio.Rx(0);
  Serial.printf("Heltec radio initialized @ %lu Hz\n", (unsigned long)RF_FREQUENCY);
  return true;
}

// ---------- Utilities ----------

/*
  Decode distance from a data buffer (big-endian).
  - If dlc >= 4: interpret as 32-bit big-endian; 0xFFFFFFFF => TIMEOUT
  - Else if dlc >= 2: interpret as 16-bit big-endian; 0xFFFF => TIMEOUT
  - Else: return 0 and indicate 'not enough data'
*/
uint32_t decodeDistanceFromBytes(const uint8_t *data, uint8_t dlc) {
  if (dlc >= 4) {
    uint32_t v = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    return v;
  } else if (dlc >= 2) {
    uint16_t v16 = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
    return (uint32_t)v16;
  } else {
    return 0; // not enough data to decode
  }
}

void printCANFrame(const twai_message_t &rx) {
  unsigned long ts = millis();
  Serial.printf("[%10lu ms] CAN RX -> ID: 0x%03X | DLC: %d | Data:", ts, rx.identifier, rx.data_length_code);
  for (int i = 0; i < rx.data_length_code; i++) Serial.printf(" %3u(0x%02X)", rx.data[i], rx.data[i]);
  Serial.println();

  // decode common ultrasonic frame (ID 0x0100) supporting 4- or 2-byte distances
  if (rx.identifier == 0x0100 && rx.data_length_code >= 2) {
    if (rx.data_length_code >= 4) {
      uint32_t dist32 = decodeDistanceFromBytes(rx.data, rx.data_length_code);
      if (dist32 == 0xFFFFFFFFUL) Serial.println("Decoded distance: TIMEOUT (0xFFFFFFFF)");
      else Serial.printf("Decoded distance (from CAN): %lu mm (32-bit)\n", (unsigned long)dist32);
    } else { // exactly 2 bytes (or >=2 but <4)
      uint32_t dist16 = decodeDistanceFromBytes(rx.data, rx.data_length_code);
      if (dist16 == 0xFFFF) Serial.println("Decoded distance: TIMEOUT (0xFFFF)");
      else Serial.printf("Decoded distance (from CAN): %u mm (16-bit)\n", (unsigned int)dist16);
    }
  }
}

// Build & send binary wrapped LoRa packet: [SRC][DST][CMD][LEN][ID_hi][ID_lo][DLC][data...][CHK]
void radioForwardCAN_asBinary(const twai_message_t &rx) {
  uint8_t buf[16];
  uint8_t idx = 0;
  buf[idx++] = BRIDGE_SRC_ID;
  buf[idx++] = BROADCAST_DST;
  buf[idx++] = CMD_CAN_FORWARD;

  uint8_t id_hi = (uint8_t)((rx.identifier >> 8) & 0xFF);
  uint8_t id_lo = (uint8_t)(rx.identifier & 0xFF);
  uint8_t dlc = rx.data_length_code > 8 ? 8 : rx.data_length_code;

  uint8_t payload_len = 3 + dlc; // ID_hi, ID_lo, DLC + data
  buf[idx++] = payload_len;
  buf[idx++] = id_hi;
  buf[idx++] = id_lo;
  buf[idx++] = dlc;
  for (uint8_t i = 0; i < dlc; ++i) buf[idx++] = rx.data[i];

  uint8_t chk = 0;
  for (uint8_t i = 0; i < idx; ++i) chk ^= buf[i];
  buf[idx++] = chk;

  Radio.Sleep(); delay(5);
  lora_idle = false;
  Radio.Send(buf, idx);
  Serial.printf("LoRa TX queued -> FORWARD CAN ID=0x%03X as binary (len=%u chk=0x%02X)\n", rx.identifier, idx, chk);
}

// ---------- Relay handling (LoRa only) ----------
void handleRelayCommandFromPayload(uint8_t src, uint8_t dst, uint8_t *payload, uint8_t len) {
  if (len < 1) {
    Serial.println("CMD_RELAY received with no payload -> ignored");
    return;
  }

  uint8_t cmd = payload[0];

  // accept both 0x01/0x00 and ASCII '1'/'0'
  if (cmd == '1') cmd = 1;
  if (cmd == '0') cmd = 0;

  if (cmd == 1) {
    digitalWrite(RELAY_PIN, HIGH);
    Serial.printf("Relay -> ON (LoRa src=0x%02X)\n", src);
    sendAck(src);
  } else if (cmd == 0) {
    digitalWrite(RELAY_PIN, LOW);
    Serial.printf("Relay -> OFF (LoRa src=0x%02X)\n", src);
    sendAck(src);
  } else {
    Serial.printf("Unknown relay payload byte: 0x%02X from 0x%02X\n", cmd, src);
  }
}

// send a simple ACK packet back to the sender
void sendAck(uint8_t to_src) {
  uint8_t pkt[5];
  uint8_t idx = 0;
  pkt[idx++] = BRIDGE_SRC_ID;
  pkt[idx++] = to_src;
  pkt[idx++] = CMD_ACK;
  pkt[idx++] = 0; // len = 0
  uint8_t chk = 0;
  for (uint8_t i = 0; i < idx; ++i) chk ^= pkt[i];
  pkt[idx++] = chk;

  Radio.Sleep(); delay(5);
  lora_idle = false;
  Radio.Send(pkt, idx);
  Serial.printf("Sent ACK to 0x%02X\n", to_src);
}

// ---------- Main loop ----------
void loop() {
  Radio.IrqProcess();

  // wait for CAN frames (with timeout)
  twai_message_t rx;
  esp_err_t r = twai_receive(&rx, TWAI_RX_TIMEOUT_TICKS);
  if (r == ESP_OK) {
    printCANFrame(rx);
    radioForwardCAN_asBinary(rx);
  } else if (r == ESP_ERR_TIMEOUT) {
    // idle - no frame
  } else {
    Serial.printf("twai_receive() error: %d\n", (int)r);
    delay(50);
  }

  delay(2);
}

// ---------- LoRa callbacks ----------
void OnTxDone(void)  { Serial.println("LoRa TX done -> back to RX"); Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle=true; }
void OnTxTimeout(void)  { Serial.println("LoRa TX timeout"); Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle=true; }

// OnRxDone: parse binary packet [SRC][DST][CMD][LEN][payload...][CHK]
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  Serial.printf("LoRa RX %u bytes, RSSI=%d, SNR=%d\n", size, rssi, snr);
  if (size < 5) {
    Serial.printf("RX too small (%u)\n", size);
    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle=true; return;
  }

  // simple XOR checksum
  uint8_t chk = 0;
  for (uint16_t i = 0; i < size - 1; ++i) chk ^= payload[i];
  if (chk != payload[size - 1]) {
    Serial.printf("RX bad checksum -> drop (calc=0x%02X pkt=0x%02X)\n", chk, payload[size-1]);
    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle=true; return;
  }

  uint8_t src = payload[0];
  uint8_t dst = payload[1];
  uint8_t cmd = payload[2];
  uint8_t len = payload[3];

  Serial.printf("Binary packet src=0x%02X dst=0x%02X cmd=0x%02X len=%u\n", src, dst, cmd, len);

  // only handle relay commands addressed to us or broadcast
  if (cmd == CMD_RELAY && (dst == BRIDGE_SRC_ID || dst == BROADCAST_DST)) {
    if (len >= 1) handleRelayCommandFromPayload(src, dst, &payload[4], len);
    else Serial.println("CMD_RELAY with no payload -> ignored");
    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle=true; return;
  }

  // handle forwarded CAN frames (print & decode if distance)
  if (cmd == CMD_CAN_FORWARD) {
    if (len >= 3) {
      uint16_t id = ((uint16_t)payload[4] << 8) | (uint16_t)payload[5];
      uint8_t dlc = payload[6];
      Serial.printf("Received wrapped CAN ID=0x%03X DLC=%u\n", id, dlc);
      Serial.print("Wrapped CAN data:");
      // Print wrapped CAN bytes (ensure we don't step past checksum)
      for (uint8_t i = 0; i < dlc && (7 + i) < size - 1; ++i) Serial.printf(" 0x%02X", payload[7 + i]);
      Serial.println();

      // compute how many data bytes actually arrived (protect against truncated packets)
      uint8_t available = 0;
      if (size > 8) { // minimal header + at least one data
        // size includes header and checksum; data starts at index 7, checksum at size-1
        if (size > 8) {
          // available bytes = (size - 1) - 7  => size - 8
          available = (uint8_t)(size - 8);
        }
      }
      // clamp to claimed dlc
      if (available > dlc) available = dlc;

      // decode distance if ID indicates ultrasonic sensor
      if (id == 0x0100 && available >= 2) {
        uint32_t dist = decodeDistanceFromBytes(&payload[7], available);
        if (available >= 4) {
          if (dist == 0xFFFFFFFFUL) Serial.println("Decoded distance (wrapped): TIMEOUT (0xFFFFFFFF)");
          else Serial.printf("Decoded distance (wrapped): %lu mm (32-bit)\n", (unsigned long)dist);
        } else { // 2-byte fallback
          if (dist == 0xFFFF) Serial.println("Decoded distance (wrapped): TIMEOUT (0xFFFF)");
          else Serial.printf("Decoded distance (wrapped): %u mm (16-bit)\n", (unsigned int)dist);
        }
      }
    } else {
      Serial.println("CMD_CAN_FORWARD payload too short");
    }
    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle=true; return;
  }

  Serial.println("Packet not relevant for bridge -> ignored");
  Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle=true;
}

void OnRxTimeout(void) { Radio.Rx(0); lora_idle=true; }
