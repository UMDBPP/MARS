#include <Wire.h>
#include <XBee.h>
#include "ccsds_xbee.h"

#define ACTUATOR_PIN 2
#define test_PIN 3
#define ARMED_LED_PIN 13
#define XBEE_ADDR 05
#define LINK_XBEE_ADDR 02
#define XBEE_PAN_ID 0x0B0B
#define CYCLE_DELAY 100 // time between execution cycles [ms]

/* function codes */
#define EXTEND_FCNCODE 0xF1
#define RETRACT_FCNCODE 0xF2

/* behavioral constants */
#define CYCLE_DELAY 100 // time between execution cycles [ms]

/* response definitions */
#define INIT_RESPONSE 0xAC
#define READ_FAIL_RESPONSE 0xAF
#define BAD_COMMAND_RESPONSE 0xBB
#define FIRED_RESPONSE 0xFF

// Interface counters
uint16_t CmdExeCtr = 0;
uint16_t CmdRejCtr = 0;
uint32_t XbeeRcvdByteCtr = 0;
uint32_t XbeeSentByteCtr = 0;

void fire(int direction, int pulse_seconds);
void read_input();
void command_response(uint8_t *fcn_code);

/*** program begin ***/


/* program functions */
void setup() {
	Serial.begin(9600);
	pinMode(test_PIN, OUTPUT);
	pinMode(ACTUATOR_PIN, OUTPUT);
  int pulse_seconds;
}

void loop() {
	// look for any new messages
	read_input();
	delay(CYCLE_DELAY);
}

void read_input() {
	int pkt_type;
	int bytes_read;
	uint8_t fcn_code;
	uint8_t incoming_bytes[100];
	if((pkt_type = readMsg(1)) == 0) {
		// Read something else, try again

	}
	// if we didn't have a read error, process it
	if (pkt_type > -1) {

		if (pkt_type) {
      fire(2,6);
      delay(20000);
			bytes_read = readCmdMsg(incoming_bytes, fcn_code);
			command_response(&fcn_code);
		}
	}
}

void fire(int direction, int pulse_seconds)
{  
    unsigned long start_milliseconds = millis();
    while (millis() <= start_milliseconds + (pulse_seconds * 1000))
    {
        digitalWrite(ACTUATOR_PIN, HIGH);
        delay(direction);
        digitalWrite(ACTUATOR_PIN, LOW);
        delay(direction);
    }

    delay(500);
}

void command_response(uint8_t *fcncode) {
  if(*fcncode == EXTEND_FCNCODE){
    fire(2,6);
  }
	// process a command to FIIIIRRRRREEEEEE!
	else if(*fcncode == RETRACT_FCNCODE){
		// fire 
		fire(1,15);
	}
}

