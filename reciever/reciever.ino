// Receiver_BinaryCmds_SimpleGPIO.ino

#include <Arduino.h>

#include "heltec.h"

#include "LoRaWan_APP.h"


// ---------------- CONFIG ----------------

#define RF_FREQUENCY 915000000UL

#define BUFFER_SIZE 128


// IDs

const uint8_t DST_ID = 0x02; // this device ID


// Commands

const uint8_t CMD_RELAY = 0x10;

const uint8_t CMD_ACK = 0x20;


// Propulsion control pin

const int PROPULSION_PIN = 26;


static RadioEvents_t RadioEvents;

volatile bool lora_idle = true;

char rcvbuf[BUFFER_SIZE];


// Forward declarations

void OnTxDone(void);

void OnTxTimeout(void);

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);

void OnRxTimeout(void);


void setup() {

Serial.begin(115200);

delay(50);

Serial.println("\nReceiver (simple GPIO) starting...");


// Pin setup

pinMode(PROPULSION_PIN, OUTPUT);

digitalWrite(PROPULSION_PIN, LOW); // ensure OFF at startup


// Heltec / Radio init

Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);


RadioEvents.TxDone = OnTxDone;

RadioEvents.TxTimeout = OnTxTimeout;

RadioEvents.RxDone = OnRxDone;

RadioEvents.RxTimeout = OnRxTimeout;


Radio.Init(&RadioEvents);

Radio.SetChannel(RF_FREQUENCY);


// TX/RX config

Radio.SetTxConfig(MODEM_LORA, 5, 0, 0, 7, 1, 8, false, true, 0, 0, false, 3000);

Radio.SetRxConfig(MODEM_LORA, 0, 7, 1, 0, 8, 0, false, 0, true, 0, 0, false, true);


lora_idle = false;

Serial.println("Starting LoRa RX...");

Radio.Rx(0);

}


void loop() {

// Service radio IRQs

Radio.IrqProcess();

delay(10);

}


// Callbacks

void OnTxDone(void) {

Radio.Sleep();

delay(2);

Radio.Rx(0);

lora_idle = true;

}


void OnTxTimeout(void) {

Radio.Sleep();

delay(2);

Radio.Rx(0);

lora_idle = true;

}


void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {

if (size < 5) {

Serial.printf("RX too small (%d)\n", size);

Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;

return;

}


// verify XOR checksum (last byte)

uint8_t chk = 0;

for (uint16_t i = 0; i < size - 1; ++i) chk ^= payload[i];

if (chk != payload[size - 1]) {

Serial.println("Bad checksum -> drop");

Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;

return;

}


uint8_t src = payload[0];

uint8_t dst = payload[1];

uint8_t cmd = payload[2];

uint8_t len = payload[3];


if (dst != DST_ID) {

Serial.printf("Packet not for me: dst=0x%02X\n", dst);

Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;

return;

}


if (cmd == CMD_RELAY && len == 1) {

uint8_t state = payload[4];

if (state == 0x01) {

digitalWrite(PROPULSION_PIN, HIGH);

Serial.printf("Propulsion -> ON (from 0x%02X)\n", src);

} else {

digitalWrite(PROPULSION_PIN, LOW);

Serial.printf("Propulsion -> OFF (from 0x%02X)\n", src);

}


// send ACK back

uint8_t ack[6];

uint8_t idx = 0;

ack[idx++] = DST_ID; // source = me

ack[idx++] = src; // dest = original sender

ack[idx++] = CMD_ACK; // ACK cmd

ack[idx++] = 0; // LEN = 0

uint8_t chk2 = 0;

for (uint8_t i = 0; i < idx; ++i) chk2 ^= ack[i];

ack[idx++] = chk2;


Serial.println("Sending ACK");

Radio.Sleep(); delay(5); lora_idle = false;

Radio.Send(ack, idx);

} else {

Serial.printf("Unhandled cmd 0x%02X len=%d\n", cmd, len);

}


Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;

}


void OnRxTimeout(void) {

Radio.Rx(0); lora_idle = true;

}