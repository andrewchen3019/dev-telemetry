// loRa_ui_receiver_compat_16bit_ui.ino
// Receives LoRa packets (wrapped CAN or simple forwarded CAN) and exposes variables to ElectricUI.
// Keeps internal distance as 32-bit, but exposes two 16-bit fields for compatibility with ElectricUI versions
// that lack EUI_UINT32.

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
uint32_t decodeDistanceFromBytes(const uint8_t *data, uint8_t dlc);

//=====================================================================================

// Import the ElectricUI Library
#include "electricui.h"

// Simple variables to modify the LED behaviour
uint8_t   propulsion = 0;
uint8_t   propulsion_before = 0;
uint8_t   propulsionState  = 0;
uint16_t  glow_time  = 200; // in milliseconds

uint32_t  led_timer  = 0;

// Keep a full 32-bit distance internally:
uint32_t distance = 0; // in millimeters (full 32-bit)

// For UI compatibility, expose distance as two uint16 fields:
uint16_t distance_hi = 0; // upper 16 bits of distance
uint16_t distance_lo = 0; // lower 16 bits of distance

// Joulemeter energy (CAN ID 0x500, bytes B4567, uint32, energy * 1000)
uint32_t joulemeter_energy = 0; // millijoules * 1000
uint16_t joulemeter_hi = 0;
uint16_t joulemeter_lo = 0;

// Instantiate the communication interface's management object
eui_interface_t serial_comms = EUI_INTERFACE( &serial_write ); 

// Electric UI manages variables referenced in this array
eui_message_t tracked_variables[] = 
{
  EUI_UINT8(  "led_blink",      propulsion ),
  EUI_UINT8(  "propulsionState", propulsionState ),
  EUI_UINT16( "lit_time",       glow_time ),
  EUI_UINT16( "distance_hi",    distance_hi ),
  EUI_UINT16( "distance_lo",    distance_lo ),
  EUI_UINT16( "joulemeter_hi",  joulemeter_hi ),
  EUI_UINT16( "joulemeter_lo",  joulemeter_lo ),
};

void setup() 
{
  Serial.begin( 115200 );
  pinMode( LED_BUILTIN, OUTPUT );

  eui_setup_interface( &serial_comms );
  EUI_TRACK( tracked_variables );
  eui_setup_identifier( "hello", 5 );

  led_timer = millis();

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
}

// ---------- helpers ----------
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

  Radio.Sleep(); delay(5);
  lora_idle = false;
  Radio.Send(pkt, idx);

  ack_received = false;
  ack_from = 0;
}

// Decode distance from a data buffer (big-endian).
uint32_t decodeDistanceFromBytes(const uint8_t *data, uint8_t dlc) {
  if (dlc >= 4) {
    uint32_t v = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | (uint32_t)data[3];
    return v;
  } else if (dlc >= 2) {
    uint16_t v16 = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
    return (uint32_t)v16;
  } else {
    return 0;
  }
}

static inline void publishDistanceToUI(uint32_t dist) {
  distance    = dist;
  distance_lo = (uint16_t)(dist & 0xFFFF);
  distance_hi = (uint16_t)((dist >> 16) & 0xFFFF);
  eui_send_tracked("distance_lo");
  eui_send_tracked("distance_hi");
}

static inline void publishJoulemeterToUI(uint32_t val) {
  joulemeter_energy = val;
  joulemeter_lo = (uint16_t)(val & 0xFFFF);
  joulemeter_hi = (uint16_t)((val >> 16) & 0xFFFF);
  eui_send_tracked("joulemeter_lo");
  eui_send_tracked("joulemeter_hi");
}

// ---------- Radio callbacks ----------
void OnTxDone(void) {
  Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
}

void OnTxTimeout(void) {
  Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
}

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  if (size < 2) {
    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
    return;
  }

  // verify XOR checksum
  if (size >= 2) {
    uint8_t chk_calc = 0;
    for (uint16_t i = 0; i < size - 1; ++i) chk_calc ^= payload[i];
    if (chk_calc != payload[size - 1]) {
      Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
      return;
    }
  }

  // Wrapped format: [SRC][DST][CMD][LEN][ID_hi][ID_lo][DLC][data...][CHK]
  if (size >= 5) {
    uint8_t src       = payload[0];
    uint8_t dst       = payload[1];
    uint8_t cmd       = payload[2];
    uint8_t len_field = payload[3];

    if ((uint16_t)(len_field + 5) == size) {

      if (cmd == CMD_ACK) {
        if (dst == SRC_ID) {
          ack_from     = src;
          ack_received = true;
        }
        Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
        return;
      }

      if (cmd == CMD_CAN_FORWARD) {
        if (len_field >= 3) {
          uint16_t can_id = ((uint16_t)payload[4] << 8) | (uint16_t)payload[5];
          uint8_t  dlc    = payload[6];
          if (dlc > 8) dlc = 8;
          uint8_t avail = len_field - 3;
          if (dlc > avail) dlc = avail;

          // Ultrasonic distance — CAN ID 0x0100, B0-3 (or B0-1 for 16-bit fallback)
          if (can_id == 0x0100 && dlc >= 2) {
            uint32_t dist = decodeDistanceFromBytes(&payload[7], dlc);
            if (dlc >= 4) {
              if (dist != 0xFFFFFFFFUL) publishDistanceToUI(dist);
            } else {
              if (dist != 0xFFFF) publishDistanceToUI(dist);
            }
          }

          // Joulemeter energy — CAN ID 0x0500, B4-7 (uint32, value * 1000)
          if (can_id == 0x0500 && dlc >= 8) {
            const uint8_t *d = &payload[7];
            uint32_t energy = ((uint32_t)d[4] << 24) | ((uint32_t)d[5] << 16) |
                              ((uint32_t)d[6] << 8)  | (uint32_t)d[7];
            if (energy != 0xFFFFFFFFUL) publishJoulemeterToUI(energy);
          }
        }
        Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
        return;
      }

      if (cmd == CMD_RELAY) {
        Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
        return;
      }

      Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
      return;
    }
  }

  // Fallback: simple forwarded CAN: [ID_hi][ID_lo][DLC][data...][CHK]
  if (size >= 4) {
    uint8_t  id_hi  = payload[0];
    uint8_t  id_lo  = payload[1];
    uint16_t can_id = ((uint16_t)id_hi << 8) | (uint16_t)id_lo;
    uint8_t  dlc    = payload[2];
    if (dlc > 8) dlc = 8;
    uint8_t possible_dlc = size - 4;
    if (dlc > possible_dlc) dlc = possible_dlc;

    // Ultrasonic distance
    if (can_id == 0x0100 && dlc >= 2) {
      uint32_t dist = decodeDistanceFromBytes(&payload[3], dlc);
      if (dlc >= 4) {
        if (dist != 0xFFFFFFFFUL) publishDistanceToUI(dist);
      } else {
        if (dist != 0xFFFF) publishDistanceToUI(dist);
      }
    }

    // Joulemeter energy
    if (can_id == 0x0500 && dlc >= 8) {
      const uint8_t *d = &payload[3];
      uint32_t energy = ((uint32_t)d[4] << 24) | ((uint32_t)d[5] << 16) |
                        ((uint32_t)d[6] << 8)  | (uint32_t)d[7];
      if (energy != 0xFFFFFFFFUL) publishJoulemeterToUI(energy);
    }

    Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
    return;
  }

  Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;
}

void OnRxTimeout(void) { Radio.Rx(0); lora_idle = true; }

//=====================================================================================
void loop() 
{
  Radio.IrqProcess();
  serial_rx_handler();

  if (propulsion != propulsion_before) {
    if (propulsion == 1) {
      sendRelayCommand(BRIDGE_ID, 0x01);
      propulsionState = 1;
    } else {
      sendRelayCommand(BRIDGE_ID, 0x00);
      propulsionState = 0;
    }
    propulsion_before = propulsion;
  }

  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  if ((now - lastCheck) > 300) {
    if (ack_received) ack_received = false;
    lastCheck = now;
  }

  delay(10);
  digitalWrite(LED_BUILTIN, propulsionState);
}

void serial_rx_handler()
{
  while (Serial.available() > 0)  
  {  
    eui_parse(Serial.read(), &serial_comms);
  }
}
  
void serial_write(uint8_t *data, uint16_t len)
{
  Serial.write(data, len);
}
