// Import the ElectricUI Library & interval-sender helper
#include "electricui.h"
#include "interval_send.h"   // <-- new include from electricui-interval-sender

// Simple variables to modify the LED behaviour
uint8_t   blink_enable = 1; // if the blinker should be running
uint8_t   led_state  = 0;   // track if the LED is illuminated
uint16_t  glow_time  = 200; // in milliseconds
uint16_t  battery_efficiency = 6; // in kWh
uint16_t  vehicle_speed = 10; //in km/h
uint32_t  led_timer  = 0;   // track when the light turned on or off

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
};

// -------------------- interval sender storage --------------------
// The interval_send helper requires a storage pool. Size '5' is ample for this demo.
// See the interval_send README for details if you want to tune pool size.
send_info_t iv_send_pool[5] = { 0 };

void setup() 
{
  // Setup the serial port and status LED
  Serial.begin( 115200 );
  pinMode( LED_BUILTIN, OUTPUT );

  // Provide the library with the interface we just setup
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

void loop() 
{
  serial_rx_handler();  //check for new inbound data

  if( blink_enable )
  {
    // Check if the LED has been on for the configured duration
    if( millis() - led_timer >= glow_time )
    {
      led_state = !led_state; //invert led state
      led_timer = millis();

      // simulate changing vehicle speed
      if(vehicle_speed == 10) vehicle_speed = 50;
      else vehicle_speed = 10;

      // simulate changing battery efficiency
      if(battery_efficiency == 6) battery_efficiency = 7;
      else battery_efficiency = 6;
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
