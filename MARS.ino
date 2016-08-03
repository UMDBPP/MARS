#include <Wire.h>
#include <XBee.h>
#include <ccsds_xbee.h>

/* physical definitions */
#define ACTUATOR_PIN 2
#define ARMED_LED_PIN 13
#define XBEE_ADDR 03
#define LINK_XBEE_ADDR 02
#define XBEE_PAN_ID 0x0B0B

/* function codes */
#define ARM_FCNCODE 0x0A
#define ARM_STATUS_FCNCODE 0x01
#define DISARM_FCNCODE 0x0D
#define FIRE_FCNCODE 0x0F

/* APIDs */
#define SCORCH_STATUS_MSG_APID 10
#define SCORCH_CMD_MSG_APID 15 // not used yet

/* behavioral constants */
#define CYCLE_DELAY 100 // time between execution cycles [ms]
#define ARM_TIMEOUT (60000/CYCLE_DELAY) // 60 * 1000 / CYCLE_DELAY

/* response definitions */
#define INIT_RESPONSE 0xAC
#define READ_FAIL_RESPONSE 0xAF
#define BAD_COMMAND_RESPONSE 0xBB
#define ARMED_RESPONSE 0xAA
#define DISARMED_RESPONSE 0xDD
#define FIRED_RESPONSE 0xFF

/* function prototypes */
void fire(int direction, int pulse_seconds);
void read_input();
void command_response(uint8_t *fcn_code);
void arm_system();
void disarm_system();
void one_byte_message(uint8_t msg);

/*** program begin ***/

/* program variables */
boolean armed;    // flag indicating if system is armed
int armed_ctr;    // counter tracking number of cycles system has been armed

/* program functions */
void setup()
{
    Serial.begin(9600);

    if (!InitXBee(XBEE_ADDR, XBEE_PAN_ID, Serial))
    {
        // it initialized
    }
    else
    {
        // you're fucked
    }

    // disarm the system before we enable the pins
    delay(500);
    disarm_system();    // also sets armed to false and armed_ctr to -1

    pinMode(ARMED_LED_PIN, OUTPUT);
    pinMode(ACTUATOR_PIN, OUTPUT);

    arm_system();

    delay(1000);

    // retract
    fire(1, 6);

    delay(2000);

    // extend to lock in string
    fire(2, 6);

    one_byte_message(INIT_RESPONSE);
}

void loop()
{
    // look for any new messages
    read_input();
    // if system is armed, increment the timer indicating for how long
    if (armed_ctr > 0)
    {
        armed_ctr++;
    }
    // if the system has been armed for more than the timeout, disarm
    if (armed_ctr > ARM_TIMEOUT)
    {
        disarm_system();
    }
    // wait
    delay(CYCLE_DELAY);
}

void read_input()
{
    int pkt_type;
    int bytes_read;
    uint8_t fcn_code;
    uint8_t incoming_bytes[100];

    if ((pkt_type = readMsg(1)) == 0)
    {
        // Read something else, try again
    }
    // if we didn't have a read error, process it
    if (pkt_type > -1)
    {
        if (pkt_type)
        {
            bytes_read = readCmdMsg(incoming_bytes, fcn_code);
            command_response(&fcn_code);
        }
        else
        {    // unknown packet type?
            one_byte_message(READ_FAIL_RESPONSE);
        }
    }
}

void command_response(uint8_t *fcncode)
{
    // process a command to arm the system
    if (*fcncode == ARM_FCNCODE)
    {
        arm_system();
    }
    // process a command to disarm the system
    else if (*fcncode == DISARM_FCNCODE)
    {
        disarm_system();
    }
    // process a command to FIIIIRRRRREEEEEE!
    else if (*fcncode == FIRE_FCNCODE)
    {
        // fire
        if (armed)
        {
            fire(1, 30);
        }
    }
    // process a command to report the arm status
    else if (*fcncode == ARM_STATUS_FCNCODE)
    {
        if (armed)
        {
            one_byte_message(ARMED_RESPONSE);
        }
        else
        {
            one_byte_message(DISARMED_RESPONSE);
        }
    }
    else
    {
        one_byte_message(BAD_COMMAND_RESPONSE);
    }
}

void arm_system()
{
    armed = true;
    digitalWrite(ARMED_LED_PIN, HIGH);
    armed_ctr = 1;

    one_byte_message(ARMED_RESPONSE);
}

void disarm_system()
{
    armed = false;
    digitalWrite(ARMED_LED_PIN, LOW);
    armed_ctr = -1;

    one_byte_message(DISARMED_RESPONSE);
}

/*
 * direction is an int, 1 = retract 2 = extend
 */
void fire(int direction, int pulse_seconds)
{
    one_byte_message(FIRED_RESPONSE);

    unsigned long start_milliseconds = millis();
    while (millis() <= start_milliseconds + (pulse_seconds * 1000))
    {
        digitalWrite(ACTUATOR_PIN, HIGH);
        delay(direction);
        digitalWrite(ACTUATOR_PIN, LOW);
        delay(direction);
    }

    delay(500);
    disarm_system();
}

void one_byte_message(uint8_t msg)
{
    uint8_t tlm_data;
    addIntToTlm<uint8_t>(msg, &tlm_data, (uint16_t) 0);
    sendTlmMsg(LINK_XBEE_ADDR, SCORCH_STATUS_MSG_APID, &tlm_data, (uint16_t) 1);
}
