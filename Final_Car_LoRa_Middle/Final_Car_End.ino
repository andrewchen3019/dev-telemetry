// can_to_heltec_lora_bridge_decodes_32bit.ino
//this is for the end that connects into the car!!
// Simplified CAN -> LoRa bridge. Forwards CAN frames as binary packets.
// Listens for LoRa CMD_RELAY commands only and toggles an attached relay.
// Forwards all CAN IDs: 0x100 (ultrasonic), 0x300 (RPM), 0x400 (throttle), 0x500 (joulemeter)

#include <Arduino.h>
#include "driver/twai.h"
#include "heltec.h"
#include "LoRaWan_APP.h"

// ---------- CONFIG ----------
#define RF_FREQUENCY 915000000UL

#define BRIDGE_SRC_ID    0x0A
#define BROADCAST_DST    0xFF

const uint8_t CMD_RELAY       = 0x10;
const uint8_t CMD_ACK         = 0x20;
const uint8_t CMD_CAN_FORWARD = 0x30;

const gpio_num_t CAN_TX_PIN = GPIO_NUM_5;
const gpio_num_t CAN_RX_PIN = GPIO_NUM_4;

const int RELAY_PIN = 26;
const bool RELAY_ACTIVE_HIGH = true;

// NOTE: confirm this matches your CAN bus speed (currently 250 kbps)
#define CAN_TIMING TWAI_TIMING_CONFIG_250KBITS()

const TickType_t TWAI_RX_TIMEOUT_TICKS = pdMS_TO_TICKS(100);
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

// ---------- Decode helpers ----------
uint32_t decodeUint32BE(const uint8_t *data) {
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8)  | (uint32_t)data[3];
}

uint16_t decodeUint16BE(const uint8_t *data) {
  return ((uint16_t)data[0] << 8) | (uint16_t)data[1];
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\nCAN->LoRa bridge starting...");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  init_can();
  init_heltec_radio();

  Serial.printf("Bridge SRC_ID=0x%02X, listening for LoRa CMD_RELAY (DST=0x%02X or BROADCAST)\n",
                BRIDGE_SRC_ID, BRIDGE_SRC_ID);
}

// ---------- CAN init ----------
void init_can() {
  Serial.printf("TWAI init -> TX pin: %d, RX pin: %d\n", CAN_TX_PIN, CAN_RX_PIN);
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t  t_config = CAN_TIMING;
  twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

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
  Serial.println("CAN initialized (TWAI @ 250 kbps)");
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

// ---------- Print & decode CAN frame for Serial monitor ----------
void printCANFrame(const twai_message_t &rx) {
  unsigned long ts = millis();
  Serial.printf("[%10lu ms] CAN RX -> ID: 0x%03X | DLC: %d | Data:",
                ts, rx.identifier, rx.data_length_code);
  for (int i = 0; i < rx.data_length_code; i++)
    Serial.printf(" %3u(0x%02X)", rx.data[i], rx.data[i]);
  Serial.println();

  const uint8_t *d   = rx.data;
  const uint8_t  dlc = rx.data_length_code;

  switch (rx.identifier) {

    case 0x0100: // Ultrasonic distance (uint32 mm, or uint16 fallback)
      if (dlc >= 4) {
        uint32_t dist = decodeUint32BE(d);
        if (dist == 0xFFFFFFFFUL) Serial.println("  [0x100] Distance: TIMEOUT");
        else Serial.printf("  [0x100] Distance: %lu mm\n", (unsigned long)dist);
      } else if (dlc >= 2) {
        uint16_t dist = decodeUint16BE(d);
        if (dist == 0xFFFF) Serial.println("  [0x100] Distance: TIMEOUT");
        else Serial.printf("  [0x100] Distance: %u mm (16-bit)\n", dist);
      }
      break;

    case 0x0300: // Motor Controller — RPM * 1000 (uint32)
      if (dlc >= 4) {
        uint32_t rpm_raw = decodeUint32BE(d);
        Serial.printf("  [0x300] RPM: %.3f\n", rpm_raw / 1000.0f);
      }
      break;

    case 0x0400: // Throttle — percent * 10 (uint16)
      if (dlc >= 2) {
        uint16_t pct_raw = decodeUint16BE(d);
        Serial.printf("  [0x400] Throttle: %.1f %%\n", pct_raw / 10.0f);
      }
      break;

    case 0x0500: // Joulemeter — current (mA), voltage (mV), energy (* 1000)
      if (dlc >= 8) {
        uint16_t current_ma = decodeUint16BE(&d[0]);
        uint16_t voltage_mv = decodeUint16BE(&d[2]);
        uint32_t energy_raw = decodeUint32BE(&d[4]);
        Serial.printf("  [0x500] Current: %.3f A | Voltage: %.3f V | Energy: %.3f J\n",
                      current_ma / 1000.0f, voltage_mv / 1000.0f, energy_raw / 1000.0f);
      }
      break;

    default:
      break;
  }
}

// ---------- Forward CAN frame over LoRa ----------
// Packet format: [SRC][DST][CMD][LEN][ID_hi][ID_lo][DLC][data...][CHK]
void radioForwardCAN_asBinary(const twai_message_t &rx) {
  uint8_t buf[16];
  uint8_t idx = 0;

  buf[idx++] = BRIDGE_SRC_ID;
  buf[idx++] = BROADCAST_DST;
  buf[idx++] = CMD_CAN_FORWARD;

  uint8_t id_hi = (uint8_t)((rx.identifier >> 8) & 0xFF);
  uint8_t id_lo = (uint8_t)(rx.identifier & 0xFF);
  uint8_t dlc   = rx.data_length_code > 8 ? 8 : rx.data_length_code;

  uint8_t payload_len = 3 + dlc; // ID_hi + ID_lo + DLC + data bytes
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
  Serial.printf("LoRa TX -> CAN ID=0x%03X forwarded (%u bytes)\n", rx.identifier, idx);
}

// ---------- Relay handling ----------
void handleRelayCommandFromPayload(uint8_t src, uint8_t dst, uint8_t *payload, uint8_t len) {
  if (len < 1) { Serial.println("CMD_RELAY: no payload -> ignored"); return; }

  uint8_t cmd = payload[0];
  if (cmd == '1') cmd = 1;
  if (cmd == '0') cmd = 0;

  if (cmd == 1) {
    digitalWrite(RELAY_PIN, HIGH);
    Serial.printf("Relay -> ON (src=0x%02X)\n", src);
    sendAck(src);
  } else if (cmd == 0) {
    digitalWrite(RELAY_PIN, LOW);
    Serial.printf("Relay -> OFF (src=0x%02X)\n", src);
    sendAck(src);
  } else {
    Serial.printf("Unknown relay byte: 0x%02X from 0x%02X\n", cmd, src);
  }
}

void sendAck(uint8_t to_src) {
  uint8_t pkt[5];
  uint8_t idx = 0;
  pkt[idx++] = BRIDGE_SRC_ID;
  pkt[idx++] = to_src;
  pkt[idx++] = CMD_ACK;
  pkt[idx++] = 0;
  uint8_t chk = 0;
  for (uint8_t i = 0; i < idx; ++i) chk ^= pkt[i];
  pkt[idx++] = chk;

  Radio.Sleep(); delay(5);
  lora_idle = false;
  Radio.Send(pkt, idx);
  Serial.printf("ACK sent to 0x%02X\n", to_src);
}

// ---------- Main loop ----------
void loop() {
  Radio.IrqProcess();

  twai_message_t rx;
  esp_err_t r = twai_receive(&rx, TWAI_RX_TIMEOUT_TICKS);
  if (r == ESP_OK) {
    printCANFrame(rx);
    radioForwardCAN_asBinary(rx);
  } else if (r != ESP_ERR_TIMEOUT) {
    Serial.printf("twai_receive() error: %d\n", (int)r);
    delay(50);
  }

  delay(2);
}

// ---------- LoRa callbacks ----------
void OnTxDone(void) {
  Serial.println("LoRa TX done -> back to RX");
  Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
}

void OnTxTimeout(void) {
  Serial.println("LoRa TX timeout");
  Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  Serial.printf("LoRa RX %u bytes, RSSI=%d, SNR=%d\n", size, rssi, snr);
  if (size < 5) {
    Serial.printf("RX too small (%u) -> drop\n", size);
    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true; return;
  }

  // Verify XOR checksum
  uint8_t chk = 0;
  for (uint16_t i = 0; i < size - 1; ++i) chk ^= payload[i];
  if (chk != payload[size - 1]) {
    Serial.printf("Bad checksum -> drop (calc=0x%02X pkt=0x%02X)\n", chk, payload[size - 1]);
    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true; return;
  }

  uint8_t src = payload[0];
  uint8_t dst = payload[1];
  uint8_t cmd = payload[2];
  uint8_t len = payload[3];

  Serial.printf("Packet src=0x%02X dst=0x%02X cmd=0x%02X len=%u\n", src, dst, cmd, len);

  if (cmd == CMD_RELAY && (dst == BRIDGE_SRC_ID || dst == BROADCAST_DST)) {
    if (len >= 1) handleRelayCommandFromPayload(src, dst, &payload[4], len);
    else Serial.println("CMD_RELAY with no payload -> ignored");
    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true; return;
  }

  // Any other packet (e.g. echoed CAN forward) — just log and ignore
  Serial.println("Packet not for bridge -> ignored");
  Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
}

void OnRxTimeout(void) { Radio.Rx(0); lora_idle = true; }
