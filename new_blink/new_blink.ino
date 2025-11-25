//Import the ElectricUI Library
#include "electricui.h"

// Simple variables to modify the LED behaviour
uint8_t   propulsion = 0; // if the blinker should be running
uint8_t   propulsion_before = 0; // if the blinker should be running
uint8_t   propulsionState  = 0;   // track if the LED is illuminated
uint16_t  glow_time  = 200; // in milliseconds

uint32_t  led_timer  = 0;   // track when the light turned on or off

// Instantiate the communication interface's management object
eui_interface_t serial_comms = EUI_INTERFACE( &serial_write ); 

// Electric UI manages variables referenced in this array
eui_message_t tracked_variables[] = 
{
  EUI_UINT8(  "led_blink",  propulsion ),
  EUI_UINT8(  "propulsionState",  propulsionState ),
  EUI_UINT16( "lit_time",   glow_time ),
};

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

  led_timer = millis();
}

void loop() 
{
  serial_rx_handler();  //check for new inbound data

  if( propulsion != propulsion_before ){
    if(propulsion == 1){
      propulsionState = 1;
    }else {
      propulsionState = 0;
    }
    propulsion_before = propulsion;
  }
  // {
  //   // Check if the LED has been on for the configured duration
  //     if( millis() - led_timer >= glow_time )
  //     {
  //          //invert led state
  //         led_timer = millis();
  //         eui_send_tracked( "propulsionState" ); // send the new value to the UI
  //     }
  // }
  digitalWrite( LED_BUILTIN, propulsionState ); //update the LED to match the intended state
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
