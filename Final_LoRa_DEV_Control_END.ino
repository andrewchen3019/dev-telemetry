// RemoteLoRa_SenderReceiver_WithRelayControl.ino
// Remote LoRa device that:
// - receives wrapped CAN forwarded packets and prints decoded ultrasonic distances
// - accepts "RELAY ON" / "RELAY OFF" on Serial and sends binary CMD_RELAY to bridge
// - waits/prints ACK from bridge

#include <Arduino.h>
#include "heltec.h"
#include "LoRaWan_APP.h"

#define RF_FREQUENCY 915000000UL

// IDs & commands
const uint8_t SRC_ID       = 0x01; // this remote device ID
const uint8_t BRIDGE_ID    = 0x0A; // bridge device ID (change if needed)
const uint8_t BROADCAST_ID = 0xFF; // broadcast (use for testing only)

const uint8_t CMD_RELAY       = 0x10;
const uint8_t CMD_ACK         = 0x20;
const uint8_t CMD_CAN_FORWARD = 0x30;

static RadioEvents_t RadioEvents;
volatile bool lora_idle = true;

// Serial input buffers
String serialBuf = "";
bool serialComplete = false;

// ACK state
volatile bool ack_received = false;
volatile uint8_t ack_from = 0;

// forward declarations
void OnTxDone(void);
void OnTxTimeout(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void OnRxTimeout(void);
void sendRelayCommand(uint8_t dst, uint8_t on_off);
uint8_t computeXor(const uint8_t *buf, uint16_t len);

// ---------- setup ----------
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\nRemote LoRa Sender/Receiver with Relay Control");

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.RxTimeout = OnRxTimeout;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);

  Radio.SetTxConfig(MODEM_LORA, 5, 0, 0, 7, 1, 8, false, true, 0, 0, false, 3000);
  Radio.SetRxConfig(MODEM_LORA, 0, 7, 1, 0, 8, 0, false, 0, true, 0, 0, false, true);

  lora_idle = false;
  Radio.Rx(0);

  Serial.println("Ready. Type 'RELAY ON' or 'RELAY OFF' on serial to command the bridge relay.");
  Serial.printf("This node SRC_ID=0x%02X -> target BRIDGE_ID=0x%02X\n", SRC_ID, BRIDGE_ID);
}

// ---------- helpers ----------
uint8_t computeXor(const uint8_t *buf, uint16_t len) {
  uint8_t chk = 0;
  for (uint16_t i = 0; i < len; ++i) chk ^= buf[i];
  return chk;
}

// Sends a CMD_RELAY (payload: 0x01 = ON, 0x00 = OFF) to dst
void sendRelayCommand(uint8_t dst, uint8_t on_off) {
  uint8_t pkt[6];
  uint8_t idx = 0;
  pkt[idx++] = SRC_ID;
  pkt[idx++] = dst;
  pkt[idx++] = CMD_RELAY;
  pkt[idx++] = 1;        // LEN
  pkt[idx++] = on_off;   // payload byte

  uint8_t chk = computeXor(pkt, idx);
  pkt[idx++] = chk;

  Serial.printf("Sending CMD_RELAY to DST=0x%02X payload=0x%02X chk=0x%02X\n", dst, on_off, chk);
  // send
  Radio.Sleep(); delay(5);
  lora_idle = false;
  Radio.Send(pkt, idx);

  // Clear ack flag then wait a short while for ACK to arrive (ACK arrival will be set in OnRxDone)
  ack_received = false;
  ack_from = 0;
}

// ---------- Radio callbacks ----------
void OnTxDone(void) {
  Serial.println("LoRa TX done -> back to RX");
  Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
}

void OnTxTimeout(void) {
  Serial.println("LoRa TX timeout");
  Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
}

// OnRxDone: handle wrapped CAN forwarded frames, simple forwarded CAN, binary ACKs, and binary relay responses
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  Serial.printf("\nLoRa RX %u bytes, RSSI=%d, SNR=%d\n", size, rssi, snr);

  if (size < 2) {
    Serial.printf("RX too small (%u)\n", size);
    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
    return;
  }

  Serial.print("Raw bytes:");
  for (uint16_t i = 0; i < size; ++i) Serial.printf(" 0x%02X", payload[i]);
  Serial.println();

  // verify XOR checksum (last byte) if size >= 2
  if (size >= 2) {
    uint8_t chk_calc = 0;
    for (uint16_t i = 0; i < size - 1; ++i) chk_calc ^= payload[i];
    if (chk_calc != payload[size - 1]) {
      Serial.printf("Checksum mismatch (calc=0x%02X pkt=0x%02X) -> drop\n", chk_calc, payload[size-1]);
      Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
      return;
    }
  }

  // Try to detect wrapped format first: [SRC][DST][CMD][LEN][ID_hi][ID_lo][DLC][data...][CHK]
  if (size >= 5) {
    uint8_t src = payload[0];
    uint8_t dst = payload[1];
    uint8_t cmd = payload[2];
    uint8_t len_field = payload[3];

    // If the size matches header + len_field + checksum, it's likely the wrapped/binary message
    if ((uint16_t)(len_field + 5) == size) {
      Serial.printf("Binary-CMD packet: SRC=0x%02X DST=0x%02X CMD=0x%02X LEN=%u\n", src, dst, cmd, len_field);

      // ACK handling
      if (cmd == CMD_ACK) {
        // is this ACK for me?
        if (dst == SRC_ID) {
          ack_from = src;
          ack_received = true;
          Serial.printf("ACK received from 0x%02X\n", src);
        } else {
          Serial.printf("ACK for someone else (dst=0x%02X)\n", dst);
        }
        Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
        return;
      }

      // CMD_CAN_FORWARD wrapped payload
      if (cmd == CMD_CAN_FORWARD) {
        // payload[4] = ID_hi, payload[5] = ID_lo, payload[6] = DLC, payload[7..] = data
        if (len_field >= 3) {
          uint16_t can_id = ((uint16_t)payload[4] << 8) | (uint16_t)payload[5];
          uint8_t dlc = payload[6];
          if (dlc > 8) dlc = 8;
          uint8_t avail = len_field - 3;
          if (dlc > avail) dlc = avail;

          Serial.printf("Wrapped CAN packet: CAN_ID=0x%03X DLC=%u\n", can_id, dlc);
          Serial.print("Wrapped CAN data:");
          for (uint8_t i = 0; i < dlc; ++i) Serial.printf(" 0x%02X", payload[7 + i]);
          Serial.println();

          // If ultrasonic ID, decode 16-bit mm
          if (can_id == 0x0100 && dlc >= 2) {
            uint16_t dist = ((uint16_t)payload[7] << 8) | (uint16_t)payload[8];
            if (dist == 0xFFFF) Serial.println("Ultrasonic: TIMEOUT (0xFFFF)");
            else Serial.printf("Ultrasonic: %u mm\n", (unsigned int)dist);
          }
        } else {
          Serial.println("CMD_CAN_FORWARD payload too short");
        }
        Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
        return;
      }

      // CMD_RELAY might be delivered to other nodes — remote doesn't act on it, just possibly print
      if (cmd == CMD_RELAY) {
        Serial.print("CMD_RELAY (binary) payload:");
        for (uint8_t i = 0; i < len_field; ++i) Serial.printf(" 0x%02X", payload[4 + i]);
        Serial.println();
        Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
        return;
      }

      // Unknown binary cmd
      Serial.printf("Binary packet CMD=0x%02X not handled\n", cmd);
      Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
      return;
    }
  }

  // Fallback: simple forwarded CAN: [ID_hi][ID_lo][DLC][data...][CHK]
  if (size >= 4) {
    uint8_t id_hi = payload[0];
    uint8_t id_lo = payload[1];
    uint16_t can_id = ((uint16_t)id_hi << 8) | (uint16_t)id_lo;
    uint8_t dlc = payload[2];
    if (dlc > 8) dlc = 8;
    // make sure we don't read past packet
    uint8_t possible_dlc = size - 4;
    if (dlc > possible_dlc) dlc = possible_dlc;

    Serial.printf("Simple forwarded CAN: CAN_ID=0x%03X DLC=%u\n", can_id, dlc);
    Serial.print("CAN data:");
    for (uint8_t i = 0; i < dlc; ++i) Serial.printf(" 0x%02X", payload[3 + i]);
    Serial.println();

    if (can_id == 0x0100 && dlc >= 2) {
      uint16_t dist = ((uint16_t)payload[3] << 8) | (uint16_t)payload[4];
      if (dist == 0xFFFF) Serial.println("Ultrasonic: TIMEOUT (0xFFFF)");
      else Serial.printf("Ultrasonic: %u mm\n", (unsigned int)dist);
    }

    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
    return;
  }

  Serial.println("Unknown packet format");
  Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
}

// OnRxTimeout
void OnRxTimeout(void) { Radio.Rx(0); lora_idle = true; }

// ---------- main loop ----------
void loop() {
  Radio.IrqProcess();

  // Serial input collection
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { serialComplete = true; break; }
    serialBuf += c;
    if (serialBuf.length() > 80) serialBuf.remove(0, serialBuf.length() - 80);
  }

  if (serialComplete) {
    String cmd = serialBuf;
    
    cmd.trim(); cmd.toUpperCase();
    if (cmd == "RELAY ON" || cmd == "RELAYON") {
      // send to bridge directly; change BRIDGE_ID -> BROADCAST_ID if you want broadcast
      sendRelayCommand(BRIDGE_ID, 0x01);
    } else if (cmd == "RELAY OFF" || cmd == "RELAYOFF") {
      sendRelayCommand(BRIDGE_ID, 0x00);
    } else {
      Serial.printf("Unknown command: %s\n", cmd.c_str());
    }
    serialBuf = "";
    serialComplete = false;
  }

  // optional: if we recently sent a command, wait a bit and show if ACK arrived
  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  if ((now - lastCheck) > 300) {
    if (ack_received) {
      Serial.printf("ACK confirmed from 0x%02X\n", ack_from);
      ack_received = false;
    }
    lastCheck = now;
  }

  delay(10);
}
