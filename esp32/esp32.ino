// Import the ElectricUI Library & interval-sender helper
#include "electricui.h"
#include "interval_send.h"   // <-- new include from electricui-interval-sender


// LORA IMPORTS
#include <Arduino.h>
#include "heltec.h"
#include "LoRaWan_APP.h"

// Simple variables to modify the LED behaviour
uint8_t   blink_enable = 1; // if the blinker should be running
uint8_t   led_state  = 0;   // track if the LED is illuminated
uint16_t  glow_time  = 120; // in milliseconds
uint16_t  battery_efficiency = 6; // in kWh
uint16_t  vehicle_speed = 10; //in km/h
uint32_t  led_timer  = 0;   // track when the light turned on or off
uint8_t propulsion = 0;
uint8_t voltage = 1;
uint8_t propulsion_before = 0;

void eui_serial_callback(uint8_t message);
// Instantiate the communication interface's management object
eui_interface_t serial_comms = EUI_INTERFACE( &serial_write ); 

// Electric UI manages variables referenced in this array
eui_message_t tracked_variables[] = 
{
  EUI_UINT8(  "led_blink",  blink_enable ),
  EUI_UINT8(  "led_state",  led_state ),
  EUI_UINT16( "lit_time",   glow_time ),
  EUI_UINT16( "battery",   battery_efficiency ),
  EUI_UINT16( "speed",   vehicle_speed),
  EUI_UINT8(  "propulsion", propulsion ),
  EUI_UINT8(  "voltage", voltage)
};

// -------------------- interval sender storage --------------------
// The interval_send helper requires a storage pool. Size '5' is ample for this demo.
// See the interval_send README for details if you want to tune pool size.
send_info_t iv_send_pool[5] = { 0 };


//CONFIG

// ---------------- CONFIG ----------------

#define RF_FREQUENCY 915000000UL

#define BUFFER_SIZE 128


#define EUI_RX_PIN 16  // data into the ESP32 from the host (UI -> ESP)
#define EUI_TX_PIN 17  // data out from ESP32 to the host (ESP -> UI)


// IDs

const uint8_t SRC_ID = 0x01; // this device

const uint8_t DST_ID = 0x02; // receiver device (target)


// Commands

const uint8_t CMD_RELAY = 0x10;

const uint8_t CMD_ACK = 0x20;


static RadioEvents_t RadioEvents;

volatile bool lora_idle = true;


// Serial input

String serialBuf = "";

bool serialComplete = false;


// ACK state (set by OnRxDone)

volatile bool ack_received = false;

volatile uint8_t ack_from = 0;


void OnTxDone(void);

void OnTxTimeout(void);

void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);

void OnRxTimeout(void);


void setup() 
{
  // Setup the serial port and status LED
  Serial.begin(115200);               // keep for debug / Heltec prints
  //Serial1.begin(115200, SERIAL_8N1, EUI_RX_PIN, EUI_TX_PIN); // Serial1 for ElectricUI
  // SETTING UP LORA
  delay(50);

  Serial.println("\nSender (simple binary) starting...");


  // Heltec radio init (same style as receiver)

  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);


  RadioEvents.TxDone = OnTxDone;

  RadioEvents.TxTimeout = OnTxTimeout;

  RadioEvents.RxDone = OnRxDone;

  RadioEvents.RxTimeout = OnRxTimeout;


  Radio.Init(&RadioEvents);

  Radio.SetChannel(RF_FREQUENCY);


  // tx/rx config

  Radio.SetTxConfig(MODEM_LORA, 5, 0, 0, 7, 1, 8, false, true, 0, 0, false, 3000);

  Radio.SetRxConfig(MODEM_LORA, 0, 7, 1, 0, 8, 0, false, 0, true, 0, 0, false, true);



  lora_idle = false;

  Radio.Rx(0);


    //Serial.println("Ready. Type 'RELAY ON' or 'RELAY OFF' on serial.");
  //----------------------
  pinMode( LED_BUILTIN, OUTPUT );

  // Provide the library with the interface we just setup
  serial_comms.interface_cb = &eui_serial_callback;

  eui_setup_interface( &serial_comms );


  // Provide the tracked variables to the library
  EUI_TRACK( tracked_variables );

  // Provide a identifier to make this board easy to find in the UI
  eui_setup_identifier( "hello", 5 );

  // ---------- interval-sender initialisation (NEW) ----------
  // Give the interval sender a pool and initialise it
  interval_send_init( iv_send_pool, 5 ); // <-- CORRECT: iv_send_pool decays to send_info_t*


  // Register the variables we want to be auto-sent at 50ms intervals
  // Match these strings exactly to the names in tracked_variables above.
  interval_send_add_id( "battery", 50 );
  interval_send_add_id( "speed", 50 );
  // ----------------------------------------------------------

  led_timer = millis();
}

// all other stuff
// Sender_BinaryCmds_Simple.ino





// all other stuff end
void eui_serial_callback( uint8_t message ) {
  // EUI_CB_TRACKED indicates a tracked message was received & applied
  Serial.println("i Have");
  if ( message == EUI_CB_TRACKED ) {
    // packet id string lives in the interface struct's packet.id_in
    // the interface variable name in your sketch is serial_comms
    char *id = (char*)serial_comms.packet.id_in;

    // compare to the messageID we care about
    if ( strcmp(id, "propulsion") == 0 ) {
      // propulsion var has already been updated by the electricui library,
      // so read propulsion and react immediately
        if (propulsion) {
          Serial.print("ahhhhhhhhhh");
          if(propulsion != propulsion_before) {
            // turn on propulsion
            uint8_t pkt[6];

            uint8_t idx = 0;

            pkt[idx++] = SRC_ID;

            pkt[idx++] = DST_ID;

            pkt[idx++] = CMD_RELAY;

            pkt[idx++] = 1; // LEN

            pkt[idx++] = 0x01; // payload: ON

            uint8_t chk = 0;

            for (uint8_t i=0;i<idx;i++) chk ^= pkt[i];

            pkt[idx++] = chk;


            Serial.println("Sending RELAY ON (binary)...");

            ack_received = false;

            Radio.Sleep(); delay(5); lora_idle = false;

            Radio.Send(pkt, idx);

            Serial.println("Send requested (waiting for ACK printed by callback)");
          }
        } else {
          Serial.print("it's off");
           if(propulsion != propulsion_before) {
            // turn off propulsion
              uint8_t pkt[6];

              uint8_t idx = 0;

              pkt[idx++] = SRC_ID;

              pkt[idx++] = DST_ID;

              pkt[idx++] = CMD_RELAY;

              pkt[idx++] = 1; // LEN

              pkt[idx++] = 0x00; // payload: OFF

              uint8_t chk = 0;

              for (uint8_t i=0;i<idx;i++) chk ^= pkt[i];

              pkt[idx++] = chk;


              Serial.println("Sending RELAY OFF (binary)...");

              ack_received = false;

              Radio.Sleep(); delay(5); lora_idle = false;

              Radio.Send(pkt, idx);

              Serial.println("Send requested (waiting for ACK printed by callback)");
            }
        }
        propulsion_before = propulsion;
      // if you want to publish confirmation back to UI:
      // eui_send_tracked("propulsion"); // (sends current value back to UI)
    }
  }
}


void loop() 
{
  serial_rx_handler();  //check for new inbound data
  Serial.println("i am here");

  if( blink_enable )
  {
    // Check if the LED has been on for the configured duration
    if( millis() - led_timer >= glow_time )
    {
      //led_state = !led_state; //invert led state
      led_timer = millis();

      // simulate changing vehicle speed
      // vehicle_speed = vehicle_speed + sin(vehicle_speed); //random(-1 * vehicle_speed * 100.0, vehicle_speed * 100.0) / 100.0;
      // battery_efficiency += battery_efficiency + sin(battery_efficiency);//random(-1 * battery_efficiency * 100.0,battery_efficiency*100.0) / 100.0;

      static float phase = 0.0f;
      phase += 0.1f; // adjust rate of oscillation

      // vary around a base value with a sine wave
      vehicle_speed = 50.0f + 20.0f * sin(phase); // 50 ± 20 km/h

      // random small jitter for realism
      vehicle_speed += random(-10, 11) / 10.0f;

      // keep battery variation too
      battery_efficiency = constrain(
        battery_efficiency + random(-2, 3), 0, 100
      );
    }    
  }

  // Let the interval sender process and dispatch any scheduled sends
  interval_send_tick( millis() );

  digitalWrite( LED_BUILTIN, led_state ); //update the LED to match the intended state
}

void serial_rx_handler()
{
  // While we have data, we will pass those bytes to the ElectricUI parser
  while( Serial.available() > 0 )  
  {  
    eui_parse( Serial.read(), &serial_comms );  // Ingest a byte
  }
}
  
void serial_write( uint8_t *data, uint16_t len )
{
  Serial.write( data, len ); //output on the main serial port
}








// CALLBACKS
void OnTxDone(void) {

Serial.println("TX done -> back to RX");

Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;

}


void OnTxTimeout(void) {

Serial.println("TX timeout");

Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;

}


void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {

// Basic sanity: need at least 5 bytes (SRC DST CMD LEN CHK)

if (size < 5) {

Serial.printf("RX too small (%d)\n", size);

Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;

return;

}


// verify XOR checksum

uint8_t chk = 0;

for (uint16_t i=0;i<size-1;i++) chk ^= payload[i];

if (chk != payload[size-1]) {

Serial.println("RX bad checksum -> drop");

Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;

return;

}


uint8_t src = payload[0];

uint8_t dst = payload[1];

uint8_t cmd = payload[2];

uint8_t len = payload[3];


// Is ACK for me?

if (dst == SRC_ID && cmd == CMD_ACK) {

ack_from = src;

ack_received = true;

} else {

Serial.printf("RX cmd=0x%02X from 0x%02X len=%d\n", cmd, src, len);

}


Radio.Sleep(); delay(2); Radio.Rx(0); lora_idle = true;

}


void OnRxTimeout(void) {

Radio.Rx(0); lora_idle = true;

}
