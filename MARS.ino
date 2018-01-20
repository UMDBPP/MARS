#include <ccsds_xbee.h>
#include "RTClib.h"  // RTC and SoftRTC

#define LINK_XBEE_ADDR 0x0002
#define XBEE_PAN_ID 0x0B0B // XBee PAN address (must be the same for all xbees)

// assign serials
#define debug_serial Serial
#define xbee_serial Serial3

// balloonduino FcnCodes
#define COMMAND_NOOP 0
#define REQUEST_PACKET_COUNTERS 10
#define COMMAND_CLEAR_PACKET_COUNTERS 15
#define REQUEST_ENVIRONMENTAL_DATA 20
#define REQUEST_POWER_DATA 30
#define REQUEST_IMU_DATA 40
#define COMMAND_REBOOT 99

/* function codes */
#define COMMAND_RETRACT_ACTUATOR 01
#define COMMAND_EXTEND_ACTUATOR 02
#define REQUEST_ACTUATOR_STATUS 03
#define COMMAND_KEEPALIVE 04
#define COMMAND_DISARM 05
#define COMMAND_ARM 06

/* response codes */
#define INIT_RESPONSE 0xAC
#define READ_FAIL_RESPONSE 0xAF
#define BAD_COMMAND_RESPONSE 0xBB
#define RETRACT_RESPONSE 0xE1
#define EXTEND_RESPONSE 0xE2
#define KEEPALIVE_RESPONSE 0xE4
#define DISARM_RESPONSE 0xE5
#define ARM_RESPONSE 0xE6

#define CYCLE_DELAY 1000 // time between execution cycles [ms]

#define green_mars

// Program Specific Constants

#ifdef green_mars

// seconds after which actuator will retract
long timeout_seconds = 600;

//// Xbee setup parameters
#define XBEE_ADDR 0x0004 // XBee address for this payload
// DO NOT CHANGE without making corresponding change in ground system definitions

/* APIDs */
#define COMMAND_APID 400
#define STATUS_APID 401
#define PACKET_COUNTER_APID 410
#define ENVIRONMENTAL_PACKET_APID 420
#define POWER_PACKET_APID 430
#define IMU_PACKET_APID 440

#endif

#ifdef black_mars

// seconds after which actuator will retract
long timeout_seconds = 600;

//// Xbee setup parameters
#define XBEE_ADDR 0x0005 // XBee address for this payload
// DO NOT CHANGE without making corresponding change in ground system definitions

/* APIDs */
#define COMMAND_APID 500
#define STATUS_APID 501
#define PACKET_COUNTER_APID 510
#define ENVIRONMENTAL_PACKET_APID 520
#define POWER_PACKET_APID 530
#define IMU_PACKET_APID 540

#endif

// create library types
RTC_DS1307 rtc;    // real time clock (for logging with timestamps)
RTC_Millis SoftRTC;    // This is the millis()-based software RTC
CCSDS_Xbee ccsds_xbee;

// pins to control actuator polarity
int HBRIDGE_DRIVER_3_4_ENABLE_PIN = 11;
int HBRIDGE_DRIVER_3_INPUT_PIN = 12;
int HBRIDGE_DRIVER_4_INPUT_PIN = 13;

bool extended = false;
bool armed = false;

uint32_t start_millis = 0;

void setup()
{
    //// Init serial ports:
    /*  aliases defined above are used to reduce confusion about which serial
     *    is connected to what interface
     *  xbee and radio serial are lower baud rates because the hardware
     *    defaults to that baud rate. higher baud rates need to be tested
     *    before they"re used with those devices
     */
    xbee_serial.begin(9600);
    debug_serial.begin(250000);

    //// RTC
    /* The RTC is used so that the log files contain timestamps. If the RTC
     *  is not running (because no battery is inserted) the RTC will be initalized
     *  to the time that this sketch was compiled at.
     */
    if (!rtc.begin())
    {
        Serial.println("RTC NOT detected.");
    }
    else
    {
        Serial.println("RTC detected!");
        if (!rtc.isrunning())
        {
            debug_serial.println("RTC is NOT running!");
            // following line sets the RTC to the date & time this sketch was compiled
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
            // To set the RTC with an explicit date & time:
            // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
        }
    }

    //// SoftRTC (for subsecond precision)
    SoftRTC.begin(rtc.now());    // Initialize SoftRTC to the current time
    start_millis = millis();    // get the current millisecond count

    if (!ccsds_xbee.init(XBEE_ADDR, XBEE_PAN_ID, xbee_serial, debug_serial))
    {
        debug_serial.print("XBee Initialized on XBee address ");
        debug_serial.println(XBEE_ADDR);
    }
    else
    {
        debug_serial.println("XBee Not Initialized");
    }

    debug_serial.println("Setting pins...");

    pinMode(HBRIDGE_DRIVER_3_4_ENABLE_PIN, OUTPUT);
    pinMode(HBRIDGE_DRIVER_3_INPUT_PIN, OUTPUT);
    pinMode(HBRIDGE_DRIVER_4_INPUT_PIN, OUTPUT);

    arm();

    digitalWrite(HBRIDGE_DRIVER_3_INPUT_PIN, LOW);
    digitalWrite(HBRIDGE_DRIVER_4_INPUT_PIN, LOW);

    retract(10);

    debug_serial.println("Waiting for 6 seconds. Insert string loop now.");

    delay(6000);

    extend(6);

    debug_serial.print("Finished setup. Timeout is ");
    debug_serial.print(timeout_seconds);
    debug_serial.println(" seconds.");
}

void loop()
{
    // if time on exceeds timeout seconds set in program constants, then retract actuator
    if (extended)
    {
        if (millis() > timeout_seconds * 1000)
        {
            debug_serial.println("Timeout exceeded.");
            retract(10);
            extended = false;
        }

        // initalize a counter to record how many bytes were read this iteration
        int BytesRead = 0;

        //// Read message from xbee

        // xbee data arrives all at the same time, so its not necessary to remember
        // it between iterations, so we use a local buffer
        uint8_t ReadData[100];

        // read the data from the xbee with a 1ms timeout
        BytesRead = ccsds_xbee.readMsg(ReadData);

        // if data was read, process it as a CCSDS packet
        if (BytesRead > 0)
        {
            debug_serial.println('Received packet.');
            // respond to the data
            command_response(ReadData, BytesRead);
        }
    }

    uint16_t pktLength = 0;
    uint8_t Pkt_Buff[100];
    uint8_t destAddr = LINK_XBEE_ADDR;

    // create a pkt
    pktLength = create_Status_pkt(Pkt_Buff, (extended) ? EXTEND_RESPONSE : RETRACT_RESPONSE);

    // send the HK packet via xbee and log it
    xbee_send_and_log(destAddr, Pkt_Buff, pktLength);

    // wait a bit
    delay(CYCLE_DELAY);
}

void arm()
{
    armed = true;
    digitalWrite(HBRIDGE_DRIVER_3_4_ENABLE_PIN, HIGH);
}

void disarm()
{
    armed = false;
    digitalWrite(HBRIDGE_DRIVER_3_4_ENABLE_PIN, LOW);
}

void extend(int pulse_seconds)
{
    debug_serial.print("Extending actuator for ");
    debug_serial.print(pulse_seconds);
    debug_serial.println(" seconds.");
    controlActuator(1, pulse_seconds);
}

void retract(int pulse_seconds)
{
    debug_serial.print("Retracting actuator for ");
    debug_serial.print(pulse_seconds);
    debug_serial.println(" seconds.");
    controlActuator(-1, pulse_seconds);
}

void controlActuator(int direction, int pulse_seconds)
{
    if (direction < 0)
    {
        digitalWrite(HBRIDGE_DRIVER_3_INPUT_PIN, LOW);
        digitalWrite(HBRIDGE_DRIVER_4_INPUT_PIN, HIGH);
    }
    else if (direction > 0)
    {
        digitalWrite(HBRIDGE_DRIVER_3_INPUT_PIN, HIGH);
        digitalWrite(HBRIDGE_DRIVER_4_INPUT_PIN, LOW);
    }

    delay(pulse_seconds * 1000);

    digitalWrite(HBRIDGE_DRIVER_3_INPUT_PIN, LOW);
    digitalWrite(HBRIDGE_DRIVER_4_INPUT_PIN, LOW);
}

void command_response(uint8_t data[], uint8_t data_len)
{
    /*  command_response()
     *
     *  given an array of data (presumably containing a CCSDS packet), check if the
     *  packet is a CAMERA command packet, and if so, process it
     */

    debug_serial.print("Rcvd: ");
    for (int i = 0; i < 8; i++)
    {
        debug_serial.print(data[i], HEX);
        debug_serial.print(", ");
    }
    debug_serial.println();

    // get the APID (the field which identifies the type of packet)
    uint16_t _APID = getAPID(data);

    // check if the data is a command packet with the Camera command APID
    if (getPacketType(data) && _APID == COMMAND_APID)
    {
        uint8_t FcnCode = getCmdFunctionCode(data);
        uint16_t pktLength = 0;
        uint8_t Pkt_Buff[100];
        uint8_t destAddr = 2;

        // respond to the command depending on what type of command it is
        switch (FcnCode)
        {

            // NoOp Cmd
            case COMMAND_NOOP:
                // No action other than to increment the interface counters

                debug_serial.println("Received NoOp Cmd");

                break;

                // HK_Req
            case COMMAND_CLEAR_PACKET_COUNTERS:
                // Requests that an HK packet be sent to the specified xbee address
                /*  Command format:
                 *   CCSDS Command Header (8 bytes)
                 *   Xbee address (1 byte)
                 */

                debug_serial.println("Received ResetCtr Cmd");

                break;
            case COMMAND_REBOOT:
                // Requests reboot
                debug_serial.println("Received Reboot Cmd");

                break;
            case COMMAND_DISARM:
                debug_serial.println("received disarm command");

                // extract the desintation address from the command
                extractFromTlm(destAddr, data, 8);

                // create a pkt
                pktLength = create_Status_pkt(Pkt_Buff, DISARM_RESPONSE);

                // send the HK packet via xbee and log it
                xbee_send_and_log(destAddr, Pkt_Buff, pktLength);

                disarm();

                break;
            case COMMAND_KEEPALIVE:
                debug_serial.println("Received keepalive packet");

                // extract the desintation address from the command
                extractFromTlm(destAddr, data, 8);

                // create a pkt
                pktLength = create_Status_pkt(Pkt_Buff, KEEPALIVE_RESPONSE);

                // send the HK packet via xbee and log it
                xbee_send_and_log(destAddr, Pkt_Buff, pktLength);

                break;
            case COMMAND_EXTEND_ACTUATOR:
                debug_serial.println("Received extend actuator command");

                // extract the desintation address from the command
                extractFromTlm(destAddr, data, 8);

                // create a pkt
                pktLength = create_Status_pkt(Pkt_Buff, EXTEND_RESPONSE);

                // send the HK packet via xbee and log it
                xbee_send_and_log(destAddr, Pkt_Buff, pktLength);

                extend(6);

                break;
            case COMMAND_RETRACT_ACTUATOR:
                debug_serial.println("Received retract actuator command");

                // extract the desintation address from the command
                extractFromTlm(destAddr, data, 8);

                // create a pkt
                pktLength = create_Status_pkt(Pkt_Buff, RETRACT_RESPONSE);

                // send the HK packet via xbee and log it
                xbee_send_and_log(destAddr, Pkt_Buff, pktLength);

                retract(30);

                break;
            case REQUEST_ACTUATOR_STATUS:
                debug_serial.println("Received actuator status request");

                // extract the desintation address from the command
                extractFromTlm(destAddr, data, 8);

                // create a pkt
                pktLength = create_Status_pkt(Pkt_Buff, (extended) ? EXTEND_RESPONSE : RETRACT_RESPONSE);

                // send the HK packet via xbee and log it
                xbee_send_and_log(destAddr, Pkt_Buff, pktLength);

                break;
            default:
                debug_serial.print("unrecognized fcn code ");
                debug_serial.println(FcnCode, HEX);
        }    // end switch(FcnCode)

    }    // if(getPacketType(data) && _APID == CAM_CMD_APID)
    else
    {
        debug_serial.print("Unrecognized ");
        debug_serial.print(getPacketType(data));
        debug_serial.print(" pkt apid 0x");
        debug_serial.println(_APID, HEX);
    }
}

void xbee_send_and_log(uint8_t dest_addr, uint8_t data[], uint8_t data_len)
{
    /*  send_and_log()
     *
     *  If the destination address is 0, calls radio_send_and_log. Otherwise
     *  calls xbee_send_and_log
     *
     */
    // send the HK packet via xbee and log it
    ccsds_xbee.sendRawData(dest_addr, data, data_len);

    debug_serial.print('Sent packet to ');
    debug_serial.println(dest_addr);
}

uint16_t create_Status_pkt(uint8_t HK_Pkt_Buff[], uint8_t message)
{
    /*  create_IMU_pkt()
     *
     *  Creates an HK packet containing the values of all the interface counters.
     *  Packet data is filled into the memory passed in as the argument
     *
     */
// get the current time from the RTC
    DateTime now = rtc.now();

// initalize counter to record length of packet
    uint16_t payloadSize = 0;

// add length of primary header
    payloadSize += sizeof(CCSDS_PriHdr_t);

// Populate primary header fields:
    setAPID(HK_Pkt_Buff, STATUS_APID);
    setSecHdrFlg(HK_Pkt_Buff, 1);
    setPacketType(HK_Pkt_Buff, 0);
    setVer(HK_Pkt_Buff, 0);
    setSeqCtr(HK_Pkt_Buff, 0);
    setSeqFlg(HK_Pkt_Buff, 0);

// add length of secondary header
    payloadSize += sizeof(CCSDS_TlmSecHdr_t);

// Populate the secondary header fields:
    setTlmTimeSec(HK_Pkt_Buff, now.unixtime() / 1000L);
    setTlmTimeSubSec(HK_Pkt_Buff, now.unixtime() % 1000L);

// Add counter values to the pkt
    payloadSize = addIntToTlm(message, HK_Pkt_Buff, payloadSize);

// fill the length field
    setPacketLength(HK_Pkt_Buff, payloadSize);

    return payloadSize;

}
