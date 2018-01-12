#include <avr/wdt.h> // watchdog timer
#include <Wire.h>
#include <SPI.h>
#include <RTClib.h> // RTC and SoftRTC
#include <SD.h>
#include <CCSDS.h>
#include <ccsds_xbee.h>
#include <ccsds_util.h>
#include <SSC.h>
#include <Xbee.h>
#include <SoftwareSerial.h>
#define mars_2


// Program Specific Constants

#ifdef mars_1

#define timer 360 // seconds after last keepalive when actuator will retract

#define ACTUATOR_CONTROL_PIN 2

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

#ifdef mars_2

#define timer 180 // seconds after last keepalive when actuator will retract

#define ACTUATOR_PIN_HBRIDGE_A 3
#define ACTUATOR_PIN_HBRIDGE_B 4

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

#define CYCLE_DELAY 100 // time between execution cycles [ms]
bool extended = false;
bool armed = true;

//// Enumerations
// logging flag
#define LOG_RCVD 1
#define LOG_SEND 0

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

// CAMERA FcnCodes
#define COMMAND_NOOP 0
#define REQUEST_PACKET_COUNTERS 10
#define COMMAND_CLEAR_PACKET_COUNTERS 15
#define REQUEST_ENVIRONMENTAL_DATA 20
#define REQUEST_POWER_DATA 30
#define REQUEST_IMU_DATA 40
#define COMMAND_REBOOT 99

#define XBEE_PAN_ID 0x0B0B // XBee PAN address (must be the same for all xbees)
// DO NOT CHANGE without changing for all xbees
#define LINK_XBEE_ADDRESS 0x0002
// DO NOT CHANGE without confirming with ground system definitions

//// Declare objects
RTC_DS1307 rtc;
RTC_Millis SoftRTC;    // This is the millis()-based software RTC
SSC ssc(0x28, 255);

//// Serial object aliases
// so that the user doesn't have to keep track of which is which
#define debug_serial Serial
#define xbee_serial Serial

//// Data Structures
// imu data
struct IMUData_s
{
        uint8_t system_calibration;
        uint8_t accel_calibration;
        uint8_t gyro_calibration;
        uint8_t mag_calibration;
        float accel_x;
        float accel_y;
        float accel_z;
        float gyro_x;
        float gyro_y;
        float gyro_z;
        float mag_x;
        float mag_y;
        float mag_z;
};
// environmental data
struct ENVData_s
{
        float bme_pres;
        float bme_temp;
        float bme_humid;
        float ssc_pres;
        float ssc_temp;
        float bno_temp;
        float mcp_temp;
};
// power data
struct PWRData_s
{
        float batt_volt;
        float i_consump;
};

//// Timing
// timing counters
uint16_t imu_read_ctr = 0;
uint16_t pwr_read_ctr = 0;
uint16_t env_read_ctr = 0;

// rate setting
// sensors will be read every X cycles
uint16_t imu_read_lim = 10;
uint16_t pwr_read_lim = 100;
uint16_t env_read_lim = 100;

// interface counters
uint16_t CmdExeCtr = 0;
uint16_t CmdRejCtr = 0;
uint32_t XbeeRcvdByteCtr = 0;
uint32_t XbeeSentByteCtr = 0;

// other variables
uint32_t start_millis = 0;
uint32_t last_keepalive_millis = 0;


uint32_t destaddr = 2;
// logging files
File xbeeLogFile;
File IMULogFile;
File ENVLogFile;
File PWRLogFile;
File initLogFile;

//// Function prototypes
void command_response(uint8_t data[], uint8_t data_len, struct IMUData_s IMUData, struct ENVData_s ENVData, struct PWRData_s PWRData);

// interface
void xbee_send_and_log(uint8_t dest_addr, uint8_t data[], uint8_t data_len);
void logPkt(File file, uint8_t data[], uint8_t len, uint8_t received_flg);

// pkt creation
uint16_t create_HK_pkt(uint8_t HK_Pkt_Buff[]);
uint16_t create_IMU_pkt(uint8_t HK_Pkt_Buff[], struct IMUData_s IMUData);
uint16_t create_PWR_pkt(uint8_t HK_Pkt_Buff[], struct PWRData_s PWRData);
uint16_t create_ENV_pkt(uint8_t HK_Pkt_Buff[], struct ENVData_s ENVData);
uint16_t create_Status_pkt(uint8_t HK_Pkt_Buff[], uint8_t message);
uint16_t _APID2;

// sensor reading
void read_imu(struct IMUData_s *IMUData);
void read_env(struct ENVData_s *ENVData);
void read_pwr(struct PWRData_s *PWRData);

// utility
void print_time(File file);

//// CODE:
void setup(void)
{
    /* setup()
     *
     * Disables watchdog timer (in case its on)
     * Initalizes all the link hardware/software including:
     *   Serial
     *   Xbee
     *   RTC
     *   SoftRTC
     *   BNO
     *   MCP
     *   BME
     *   SSC
     *   ADS
     *   SD card
     *   Log files
     */
    // disable the watchdog timer immediately in case it was on because of a
    // commanded reboot
    wdt_disable();

    //// Init serial ports:
    /*  aliases defined above are used to reduce confusion about which serial
     *    is connected to what interface
     *  xbee serial is lower baud rates because the hardware
     *    defaults to that baud rate. higher baud rates need to be tested
     *    before they're used with those devices
     */
    debug_serial.begin(250000);
    xbee_serial.begin(9600);

    debug_serial.println("GoGoGadget Camera payload!");


    //// RTC
    /* The RTC is used so that the log files contain timestamps. If the RTC
     *  is not running (because no battery is inserted) the RTC will be initalized
     *  to the time that this sketch was compiled at.
     */
    if (!rtc.begin())
    {
        debug_serial.println("DS1308 NOT detected.");
    }
    else
    {
        debug_serial.println("DS1308 detected!");
    }

    if (!rtc.isrunning())
    {
        debug_serial.println("RTC is NOT running!");
        // following line sets the RTC to the date & time this sketch was compiled
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        // This line sets the RTC with an explicit date & time, for example to set
        // January 21, 2014 at 3am you would call:
        // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
    }

    //// SoftRTC (for subsecond precision)
    SoftRTC.begin(rtc.now());    // Initialize SoftRTC to the current time
    start_millis = millis();    // get the current millisecond count

    //// Init SD card
    SPI.begin();
    pinMode(53, OUTPUT);
    if (!SD.begin(53))
    {
        debug_serial.println("SD Card NOT detected.");
    }
    else
    {
        debug_serial.println("SD Card detected!");
    }

    // xbee
    debug_serial.println("Beginning xbee init");

    int xbeeStatus = InitXBee(XBEE_ADDR, XBEE_PAN_ID, xbee_serial, false);
    if (!xbeeStatus)
    {
        debug_serial.println("XBee Initialized!");
    }
    else
    {
        debug_serial.print("XBee Failed to Initialize with Error Code: ");
        debug_serial.println(xbeeStatus);
    }

    //// Init SSC
    //  set min / max reading and pressure, see datasheet for the values for your
    //  sensor
    ssc.setMinRaw(0);
    ssc.setMaxRaw(16383);
    ssc.setMinPressure(0.0);
    ssc.setMaxPressure(30);

    //  start the sensor
    debug_serial.print("SSC start: ");
    debug_serial.println(ssc.start());

    //MicroSD
    // appends to current file
    // NOTE: Filenames must be shorter than 8 characters


#ifdef mars_1
    pinMode(ACTUATOR_CONTROL_PIN, OUTPUT);

    digitalWrite(ACTUATOR_CONTROL_PIN, LOW);
#endif

#ifdef mars_2
    pinMode(ACTUATOR_PIN_HBRIDGE_A, OUTPUT);
    pinMode(ACTUATOR_PIN_HBRIDGE_B, OUTPUT);

    digitalWrite(ACTUATOR_PIN_HBRIDGE_A, LOW);
    digitalWrite(ACTUATOR_PIN_HBRIDGE_B, LOW);
#endif

    retract(10);
    delay(6000);
    extend(6);

    armed = true;
}

void loop(void)
{
    /*  loop()
     *
     *  Reads sensor
     *  Log sensors
     *  Reads from xbee and processes any data
     */



// initalize a counter to record how many bytes were read this iteration
    int BytesRead = 0;

//// Read message from xbee

// xbee data arrives all at the same time, so its not necessary to remember
// it between iterations, so we use a local buffer
    uint8_t ReadData[100];

// read the data from the xbee with a 1ms timeout
    BytesRead = _readXbeeMsg(ReadData, 1);

// if data was read, record it in the Xbee Rcvd counter
    if (BytesRead > 0)
    {
        XbeeRcvdByteCtr += BytesRead;
    }

// if data was read, process it as a CCSDS packet
    if (BytesRead > 0)
    {
        // log the received data
        logPkt(xbeeLogFile, ReadData, BytesRead, LOG_RCVD);

    }

// if time on exceeds timer set in program constants, then retract actuator

// wait a bit
    delay(10);

}

void command_response(uint8_t data[], uint8_t data_len, struct IMUData_s IMUData, struct ENVData_s ENVData, struct PWRData_s PWRData)
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

                // increment the cmd executed counter
                CmdExeCtr++;
                break;

                // HK_Req
            case REQUEST_PACKET_COUNTERS:
                // Requests that an HK packet be sent to the specified xbee address
                /*  Command format:
                 *   CCSDS Command Header (8 bytes)
                 *   Xbee address (1 byte)
                 */

                debug_serial.println("Received HK_Req Cmd");

                // extract the desintation address from the command
                extractFromTlm(destAddr, data, 8);

                // create a HK pkt
                pktLength = create_HK_pkt(Pkt_Buff);

                // send the HK packet via xbee and log it
                xbee_send_and_log(destAddr, Pkt_Buff, pktLength);

                // increment the cmd executed counter
                CmdExeCtr++;
                break;

                // ResetCtr
            case COMMAND_CLEAR_PACKET_COUNTERS:
                // Requests that an HK packet be sent to the specified xbee address
                /*  Command format:
                 *   CCSDS Command Header (8 bytes)
                 *   Xbee address (1 byte)
                 */

                debug_serial.println("Received ResetCtr Cmd");

                CmdExeCtr = 0;
                CmdRejCtr = 0;
                XbeeRcvdByteCtr = 0;
                XbeeSentByteCtr = 0;

                // increment the cmd executed counter
                CmdExeCtr++;
                break;

                // ENV_Req
           
            case COMMAND_REBOOT:
                // Requests that Link reboot
                debug_serial.println("Received Reboot Cmd");

                // set the reboot timer
                wdt_enable(WDTO_1S);

                // increment the cmd executed counter
                CmdExeCtr++;
                break;

                // unrecognized fcn code

            case COMMAND_DISARM:
                debug_serial.println("received disarm command");

                // extract the desintation address from the command
                extractFromTlm(destAddr, data, 8);

                // create a pkt
                pktLength = create_Status_pkt(Pkt_Buff, DISARM_RESPONSE);

                // send the HK packet via xbee and log it
                xbee_send_and_log(destAddr, Pkt_Buff, pktLength);

                armed = false;
                break;

            case COMMAND_KEEPALIVE:
                debug_serial.println("Received keepalive packet");

                // extract the desintation address from the command
                extractFromTlm(destAddr, data, 8);

                // create a pkt
                pktLength = create_Status_pkt(Pkt_Buff, KEEPALIVE_RESPONSE);

                // send the HK packet via xbee and log it
                xbee_send_and_log(destAddr, Pkt_Buff, pktLength);

                last_keepalive_millis = millis();
                break;
                
            case COMMAND_EXTEND_ACTUATOR:
                debug_serial.println("Received extend actuator command");

                // extract the desintation address from the command
                extractFromTlm(destAddr, data, 8);

                // create a pkt
                pktLength = create_Status_pkt(Pkt_Buff, EXTEND_RESPONSE);
                destaddr = 2;

                // send the HK packet via xbee and log it
                xbee_send_and_log(destAddr, Pkt_Buff, pktLength);

                extend(6);

                // increment the cmd executed counter
                CmdExeCtr++;
                break;
            case COMMAND_RETRACT_ACTUATOR:
                debug_serial.println("Received retract actuator command");
                destaddr = 2;

                // extract the desintation address from the command
                extractFromTlm(destAddr, data, 8);

                // create a pkt
                pktLength = create_Status_pkt(Pkt_Buff, RETRACT_RESPONSE);

                // send the HK packet via xbee and log it
                xbee_send_and_log(destAddr, Pkt_Buff, pktLength);
                retract(30);
                // increment the cmd executed counter
                CmdExeCtr++;
                break;
            case REQUEST_ACTUATOR_STATUS:
                debug_serial.println("Received actuator status request");

                // extract the desintation address from the command
                extractFromTlm(destAddr, data, 8);

                // create a pkt
                pktLength = create_Status_pkt(Pkt_Buff, (extended) ? EXTEND_RESPONSE : RETRACT_RESPONSE);

                // send the HK packet via xbee and log it
                xbee_send_and_log(destAddr, Pkt_Buff, pktLength);

                // increment the cmd executed counter
                CmdExeCtr++;
                break;
            default:
                debug_serial.print("unrecognized fcn code ");
                debug_serial.println(FcnCode, HEX);

                // reject command
                CmdRejCtr++;
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


uint16_t create_HK_pkt(uint8_t HK_Pkt_Buff[])
{
    /*  create_HK_pkt()
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
    setAPID(HK_Pkt_Buff, PACKET_COUNTER_APID);
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
    payloadSize = addIntToTlm(CmdExeCtr, HK_Pkt_Buff, payloadSize);    // Add counter of sent packets to message
    payloadSize = addIntToTlm(CmdRejCtr, HK_Pkt_Buff, payloadSize);    // Add counter of sent packets to message
    payloadSize = addIntToTlm(XbeeRcvdByteCtr, HK_Pkt_Buff, payloadSize);    // Add counter of sent packets to message
    payloadSize = addIntToTlm(XbeeSentByteCtr, HK_Pkt_Buff, payloadSize);    // Add counter of sent packets to message
    payloadSize = addIntToTlm(millis() / 1000L, HK_Pkt_Buff, payloadSize);    // Timer

// fill the length field
    setPacketLength(HK_Pkt_Buff, payloadSize);

    return payloadSize;

}




void xbee_send_and_log(uint8_t dest_addr, uint8_t data[], uint8_t data_len)
{
    /*  xbee_send_and_log()
     *
     *  Sends the given data out over the xbee and adds an entry to the xbee log file.
     *  Also updates the radio sent counter.
     */

// send the data via xbee
    _sendData(dest_addr, data, data_len);

    debug_serial.print("Forwarding: ");
    for (int i = 0; i <= data_len; i++)
    {
        debug_serial.print(data[i], HEX);
        debug_serial.print(", ");
    }
    debug_serial.println();

// log the sent data
    logPkt(xbeeLogFile, data, sizeof(data), 0);

// update the xbee send ctr
    XbeeSentByteCtr += data_len;
}

void logPkt(File file, uint8_t data[], uint8_t len, uint8_t received_flg)
{
    /*  logPkt()
     *
     *  Prints an entry in the given log file containing the given data. Will prepend an
     *  'S' if the data was sent or an 'R' is the data was received based on the value
     *  of the received_flg.
     */

// if the file is open
    if (file)
    {

        // prepend an indicator of if the data was received or sent
        // R indicates this was received data
        if (received_flg)
        {
            file.print("R ");
        }
        else
        {
            file.print("S ");
        }

        // Print a timestamp
        print_time(file);

        char buf[50];

        // print the data in hex
        file.print(": ");
        for (int i = 0; i < len; i++)
        {
            sprintf(buf, "%02x, ", data[i]);
            file.print(buf);
        }
        file.println();

        // ensure the data gets written to the file
        file.flush();
    }
}

void print_time(File file)
{
    /*  print_time()
     *
     *  Prints the current time to the given log file
     */

// get the current time from the RTC
    DateTime now = rtc.now();

// print a datestamp to the file
    char buf[50];
    sprintf(buf, "%02d/%02d/%02d %02d:%02d:%02d", now.day(), now.month(), now.year(), now.hour(), now.minute(), now.second());
    file.print(buf);
}

void extend(int pulse_seconds)
{
    controlActuator(1, pulse_seconds);
    extended = true;
}

void retract(int pulse_seconds)
{
    controlActuator(-1, pulse_seconds);
    extended = false;
}

void controlActuator(int direction, int pulse_seconds)
{
#ifdef mars_1
// actuator with built-in polarity switch
    int frequency = 0;
    if (direction < 0)
    {
        frequency = 1;
    }
    else if (direction > 0)
    {
        frequency = 2;
    }

    unsigned long start_milliseconds = millis();

// send signal: 1 ms retract, 2 ms extend
    while (millis() <= start_milliseconds + (pulse_seconds * 1000))
    {
        digitalWrite(ACTUATOR_CONTROL_PIN, HIGH);
        delay(frequency);
        digitalWrite(ACTUATOR_CONTROL_PIN, LOW);
        delay(frequency);
    }
#endif

#ifdef mars_2
// actuator without built-in polarity switch
    if (direction < 0)
    {
        digitalWrite(ACTUATOR_PIN_HBRIDGE_A, LOW);
        digitalWrite(ACTUATOR_PIN_HBRIDGE_B, HIGH);
    }
    else if (direction > 0)
    {
        digitalWrite(ACTUATOR_PIN_HBRIDGE_A, HIGH);
        digitalWrite(ACTUATOR_PIN_HBRIDGE_B, LOW);
    }

    delay(pulse_seconds * 1000);

    digitalWrite(ACTUATOR_PIN_HBRIDGE_A, LOW);
    digitalWrite(ACTUATOR_PIN_HBRIDGE_B, LOW);

#endif
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
