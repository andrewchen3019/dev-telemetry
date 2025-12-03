/* RemoteLoRa_WithElectricUI_RSSI_Fix.ino
   ESP32 Remote LoRa node with ElectricUI Embedded support.

   This variant avoids using EUI_INT16 (which caused a macro initializer error)
   by publishing RSSI as a uint16 (rssi + 256). The UI will subtract 256 to
   display signed dBm.
*/

#include <Arduino.h>
#include "heltec.h"
#include "LoRaWan_APP.h"

#include "electricui.h"
#include "interval_send.h"   // optional helper to auto-send values

#define RF_FREQUENCY 915000000UL  // adjust as needed

// Device IDs & commands
const uint8_t SRC_ID       = 0x01;
const uint8_t BRIDGE_ID    = 0x0A;
const uint8_t BROADCAST_ID = 0xFF;

const uint8_t CMD_RELAY       = 0x10;
const uint8_t CMD_ACK         = 0x20;
const uint8_t CMD_CAN_FORWARD = 0x30;

// LoRa radio
static RadioEvents_t RadioEvents;
volatile bool lora_idle = true;

// Serial input
String serialBuf = "";
bool serialComplete = false;

// ACK state
volatile bool ack_received = false;
volatile uint8_t ack_from = 0;

// --- ElectricUI tracked vars ---
uint8_t propulsion = 0;
uint8_t propulsion_last = 0xFF;
uint16_t ultrasonic = 0;
uint16_t battery = 6;
uint16_t speed = 10;
uint8_t led_state = 0;
uint16_t lit_time = 120;
uint8_t voltage = 1;

// RSSI: keep signed int for internal, but publish offset version as uint16
int16_t rssi_tracked = 0;
uint16_t rssi_offset = 0; // rssi_tracked + 256

// ElectricUI interface & tracked array
void serial_write(uint8_t *data, uint16_t len);
void eui_serial_callback(uint8_t message);

eui_interface_t serial_comms = EUI_INTERFACE(&serial_write);

eui_message_t tracked_variables[] = {
  EUI_UINT8("propulsion", propulsion),
  EUI_UINT16("ultrasonic", ultrasonic),
  EUI_UINT16("battery", battery),
  EUI_UINT16("speed", speed),
  EUI_UINT8("led_state", led_state),
  EUI_UINT16("lit_time", lit_time),
  EUI_UINT8("voltage", voltage),
  // Use uint16 to avoid EUI_INT16 macro issues; UI will subtract 256 to get signed dBm.
  EUI_UINT16("rssi", rssi_offset),
};

// interval-send pool
send_info_t iv_send_pool[6] = {0};

// --- Forward declarations ---
void OnTxDone(void);
void OnTxTimeout(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
void OnRxTimeout(void);
void sendRelayCommand(uint8_t dst, uint8_t on_off);
uint8_t computeXor(const uint8_t *buf, uint16_t len);

void serial_rx_handler();

// ---------- Helpers ----------
uint8_t computeXor(const uint8_t *buf, uint16_t len) {
  uint8_t chk = 0;
  for (uint16_t i = 0; i < len; ++i) chk ^= buf[i];
  return chk;
}

void sendRelayCommand(uint8_t dst, uint8_t on_off) {
  uint8_t pkt[6];
  uint8_t idx = 0;
  pkt[idx++] = SRC_ID;
  pkt[idx++] = dst;
  pkt[idx++] = CMD_RELAY;
  pkt[idx++] = 1;
  pkt[idx++] = on_off;
  uint8_t chk = computeXor(pkt, idx);
  pkt[idx++] = chk;

  Serial.printf("Sending CMD_RELAY to DST=0x%02X payload=0x%02X chk=0x%02X\n", dst, on_off, chk);
  Radio.Sleep();
  delay(5);
  lora_idle = false;
  Radio.Send(pkt, idx);
  ack_received = false;
  ack_from = 0;
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
  Serial.printf("\nLoRa RX %u bytes, RSSI=%d, SNR=%d\n", size, rssi, snr);

  if (size < 2) {
    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
    return;
  }

  // Verify checksum if expected
  if (size >= 2) {
    uint8_t chk_calc = 0;
    for (uint16_t i = 0; i < size - 1; ++i) chk_calc ^= payload[i];
    if (chk_calc != payload[size - 1]) {
      Serial.println("Checksum mismatch -> drop");
      Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
      return;
    }
  }

  // Update tracked RSSI (signed dBm) and publish offsetted uint16
  rssi_tracked = rssi;
  // offset to make positive
  rssi_offset = (uint16_t)((int32_t)rssi_tracked + 256);
  eui_send_tracked("rssi");

  // Parse wrapped CAN forwarded frames (ultrasonic)
  if (size >= 5) {
    uint8_t src = payload[0];
    uint8_t dst = payload[1];
    uint8_t cmd = payload[2];
    uint8_t len_field = payload[3];

    if ((uint16_t)(len_field + 5) == size) {
      if (cmd == CMD_ACK) {
        if (dst == SRC_ID) {
          ack_from = src; ack_received = true;
        }
      } else if (cmd == CMD_CAN_FORWARD) {
        if (len_field >= 3) {
          uint16_t can_id = ((uint16_t)payload[4] << 8) | (uint16_t)payload[5];
          uint8_t dlc = payload[6];
          if (dlc > 8) dlc = 8;
          uint8_t avail = len_field - 3;
          if (dlc > avail) dlc = avail;

          if (can_id == 0x0100 && dlc >= 2) {
            uint16_t dist = ((uint16_t)payload[7] << 8) | (uint16_t)payload[8];
            ultrasonic = dist;
            eui_send_tracked("ultrasonic");
            if (dist == 0xFFFF) Serial.println("Ultrasonic: TIMEOUT");
            else Serial.printf("Ultrasonic: %u mm\n", dist);
          }
        }
      }
    }
  } else {
    // fallback simple CAN format
    if (size >= 4) {
      uint8_t id_hi = payload[0];
      uint8_t id_lo = payload[1];
      uint16_t can_id = ((uint16_t)id_hi << 8) | (uint16_t)id_lo;
      uint8_t dlc = payload[2];
      if (dlc > 8) dlc = 8;
      uint8_t possible_dlc = size - 4;
      if (dlc > possible_dlc) dlc = possible_dlc;

      if (can_id == 0x0100 && dlc >= 2) {
        uint16_t dist = ((uint16_t)payload[3] << 8) | (uint16_t)payload[4];
        ultrasonic = dist;
        eui_send_tracked("ultrasonic");
        if (dist == 0xFFFF) Serial.println("Ultrasonic: TIMEOUT");
        else Serial.printf("Ultrasonic: %u mm\n", dist);
      }
    }
  }

  Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
}

void OnRxTimeout(void) { Radio.Rx(0); lora_idle = true; }

// ---------- ElectricUI glue ----------
// void eui_serial_callback(uint8_t message) {
//   if (message == EUI_CB_TRACKED) {
//     char *id = (char*)serial_comms.packet.id_in;
//     if (strcmp(id, "propulsion") == 0) {
//       Serial.printf("ElectricUI propulsion changed -> %u\n", propulsion);
//       if (propulsion != propulsion_last) {
//         propulsion_last = propulsion;
//         sendRelayCommand(BRIDGE_ID, propulsion ? 0x01 : 0x00);
//       }
//     }
//   }
// }

void eui_serial_callback(uint8_t message) {
    if (message == EUI_CB_TRACKED) {
      const char key[] = "propulsion";
      if (serial_comms.packet.id_in_len == sizeof(key)-1 &&
      memcmp(serial_comms.packet.id_in, key, sizeof(key)-1) == 0) {
        Serial.printf("ElectricUI propulsion changed -> %u\n", propulsion);
        if (propulsion != propulsion_last) {
          propulsion_last = propulsion;
          sendRelayCommand(BRIDGE_ID, propulsion ? 0x01 : 0x00);
        }
      }
    }
}
void serial_rx_handler() {
  while (Serial.available() > 0) {
    eui_parse(Serial.read(), &serial_comms);
  }
}

void serial_write(uint8_t *data, uint16_t len) {
  Serial.write(data, len);
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\nESP32 LoRa Remote with ElectricUI (RSSI fixed)");

  // LoRa init
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxDone = OnRxDone;
  RadioEvents.RxTimeout = OnRxTimeout;
  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, 5, 0, 0, 7, 1, 8, false, true, 0, 0, false, 3000);
  Radio.SetRxConfig(MODEM_LORA, 0, 7, 1, 0, 8, 0, false, 0, true, 0, 0, false, true);
  Radio.Rx(0);

  // ElectricUI setup
  serial_comms.interface_cb = &eui_serial_callback;
  eui_setup_interface(&serial_comms);
  EUI_TRACK(tracked_variables);
  eui_setup_identifier("remote_01", 9);

  // interval sender
  interval_send_init(iv_send_pool, 6);
  interval_send_add_id("battery", 50);
  interval_send_add_id("speed", 50);
  interval_send_add_id("ultrasonic", 50);
  interval_send_add_id("rssi", 50); // publish rssi as timeseries

  Serial.printf("Remote SRC_ID=0x%02X -> BRIDGE_ID=0x%02X\n", SRC_ID, BRIDGE_ID);
  Serial.println("Ready.");
}

// ---------- Main loop ----------
void loop() {
  Radio.IrqProcess();
  serial_rx_handler();

  // CLI handling

  // if (propulsion != propulsion_last) {
  //   sendRelayCommand(BRIDGE_ID, propulsion ? 0x01 : 0x00);
  //   propulsion_last = propulsion;
  //   Serial.printf("ahhhhhh")
  // }

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
      sendRelayCommand(BRIDGE_ID, 0x01);
      propulsion = 1;
      eui_send_tracked("propulsion");
    } else if (cmd == "RELAY OFF" || cmd == "RELAYOFF") {
      sendRelayCommand(BRIDGE_ID, 0x00);
      propulsion = 0;
      eui_send_tracked("propulsion");
    } else {
      Serial.printf("Unknown command: %s\n", cmd.c_str());
    }
    serialBuf = "";
    serialComplete = false;
  }

  // ACK display
  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  if ((now - lastCheck) > 300) {
    if (ack_received) {
      Serial.printf("ACK confirmed from 0x%02X\n", ack_from);
      ack_received = false;
    }
    lastCheck = now;
  }

  // interval send tick
  interval_send_tick(millis());
  delay(10);
}
