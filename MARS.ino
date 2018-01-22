//// Includes:
#include <avr/wdt.h>  // watchdog timer
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#include "Adafruit_MCP9808.h"
#include "RTClib.h"  // RTC and SoftRTC
#include <Adafruit_BME280.h>
#include <SD.h>
#include <Adafruit_ADS1015.h>
#include <ccsds_xbee.h>
#include <SSC.h>

// Name
#define PAYLOAD_NAME "MARS"
// Must be no longer than 8 characters

#define green_mars

#define LINK_XBEE_ADDR 0x0002
#define XBEE_PAN_ID 0x0B0B // XBee PAN address (must be the same for all xbees)

// assign serials
#define debug_serial Serial
#define xbee_serial Serial3

// balloonduino FcnCodes
#define COMMAND_NOOP 0
#define REQUEST_PACKET_COUNTERS 10
#define COMMAND_CLEAR_PACKET_COUNTERS 15
#define REQUEST_PAYLOAD_NAME 19
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

#define CYCLE_DELAY 100 // time between execution cycles [ms]

// green MARS is holding TARDIS on NS74
#ifdef green_mars

// seconds after which actuator will retract
long timeout_seconds = 1020;

//// Xbee setup parameters
#define XBEE_ADDR 0x0004 // XBee address for this payload
// DO NOT CHANGE without making corresponding change in ground system definitions

/* APIDs */
#define COMMAND_APID 400
#define STATUS_APID 401
#define PACKET_COUNTER_APID 410
#define PAYLOAD_NAME_APID 419
#define ENVIRONMENTAL_PACKET_APID 420
#define POWER_PACKET_APID 430
#define IMU_PACKET_APID 440

#endif

// black MARS is holding the entire payload string on NS74
#ifdef black_mars

// seconds after which actuator will retract
long timeout_seconds = 1200;

//// Xbee setup parameters
#define XBEE_ADDR 0x0005 // XBee address for this payload
// DO NOT CHANGE without making corresponding change in ground system definitions

/* APIDs */
#define COMMAND_APID 500
#define STATUS_APID 501
#define PACKET_COUNTER_APID 510
#define PAYLOAD_NAME_APID 519
#define ENVIRONMENTAL_PACKET_APID 520
#define POWER_PACKET_APID 530
#define IMU_PACKET_APID 540

#endif

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

//// Interface counters
// counters to track what data comes into/out of link
uint16_t CmdExeCtr;
uint16_t CmdRejCtr;
uint32_t XbeeRcvdByteCtr;
uint32_t XbeeSentByteCtr;

//// Declare objects
Adafruit_BNO055 bno = Adafruit_BNO055(-1, 0x29);
Adafruit_MCP9808 tempsensor = Adafruit_MCP9808();
RTC_DS1307 rtc;  // real time clock (for logging with timestamps)
RTC_Millis SoftRTC;   // This is the millis()-based software RTC
Adafruit_BME280 bme;
Adafruit_ADS1015 ads(0x4A);
SSC ssc(0x28, 255);
CCSDS_Xbee ccsds_xbee;

//// Data Structures
// imu data
struct IMUData_s
{
        uint8_t system_cal;
        uint8_t accel_cal;
        uint8_t gyro_cal;
        uint8_t mag_cal;
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

// power data
struct PWRData_s
{
        float batt_volt;
        float i_consump;
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
// environmental data
struct InitStat_s
{
        uint8_t xbeeStatus;
        uint8_t rtc_running;
        uint8_t rtc_start;
        uint8_t BNO_init;
        uint8_t MCP_init;
        uint8_t BME_init;
        uint8_t SSC_init;
        uint8_t SD_detected;
};

InitStat_s InitStat;

//// Files
// interface logging files
File xbeeLogFile;
File initLogFile;
// data logging files
File IMULogFile;
File ENVLogFile;
File PWRLogFile;

// pins to control actuator polarity
int HBRIDGE_DRIVER_3_4_ENABLE_PIN = 11;
int HBRIDGE_DRIVER_3_INPUT_PIN = 12;
int HBRIDGE_DRIVER_4_INPUT_PIN = 13;

bool extended = false;
bool armed = false;

uint32_t start_millis = 0;

void setup()
{
    /* setup()
     *
     * Disables watchdog timer (in case its on)
     * Initalizes all the balloonduino hardware/software including:
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
     *  xbee and radio serial are lower baud rates because the hardware
     *    defaults to that baud rate. higher baud rates need to be tested
     *    before they're used with those devices
     */
    debug_serial.begin(250000);
    xbee_serial.begin(9600);

    debug_serial.println("Begin MARS Init");

    //// RTC
    /* The RTC is used so that the log files contain timestamps. If the RTC
     *  is not running (because no battery is inserted) the RTC will be initalized
     *  to the time that this sketch was compiled at.
     */
    InitStat.rtc_start = rtc.begin();
    if (!InitStat.rtc_start)
    {
        Serial.println("RTC NOT detected.");
    }
    else
    {
        Serial.println("RTC detected!");
        InitStat.rtc_running = rtc.isrunning();
        if (!InitStat.rtc_running)
        {
            debug_serial.println("RTC is NOT running!");
            // following line sets the RTC to the date & time this sketch was compiled
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
            // To set the RTC with an explicit date & time:
            // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
        }
    }

    //// SoftRTC (for subsecond precision)
    SoftRTC.begin(rtc.now());  // Initialize SoftRTC to the current time
    start_millis = millis();  // get the current millisecond count

    //// Init SD card
    /* The SD card is used to store all of the log files.
     */
    SPI.begin();
    pinMode(53, OUTPUT);
    InitStat.SD_detected = SD.begin(53);
    if (!InitStat.SD_detected)
    {
        debug_serial.println("SD Card NOT detected.");
    }
    else
    {
        debug_serial.println("SD Card detected!");
    }

    //// Open log files
    /* Link will log to 3 files, one for I/O to the xbee, one for I/O to the radio,
     *  and one for recording its initialization status each time it starts up.
     *  NOTE: Filenames must be shorter than 8 characters
     */

    xbeeLogFile = SD.open("XBEE_LOG.txt", FILE_WRITE);
    delay(10);

    // for data files, write a header
    initLogFile = SD.open("INIT_LOG.txt", FILE_WRITE);
    initLogFile.println("DateTime,RTCStart,RTCRun,BNO,BME,MCP,SSC,Xbee");
    initLogFile.flush();
    delay(10);
    IMULogFile = SD.open("IMU_LOG.txt", FILE_WRITE);
    IMULogFile.println(
            "DateTime,SystemCal[0-3],AccelCal[0-3],GyroCal[0-3],MagCal[0-3],AccelX[m/s^2],AccelY[m/s^2],AccelZ[m/s^2],GyroX[rad/s],GyroY[rad/s],GyroZ[rad/s],MagX[uT],MagY[uT],MagZ[uT]");
    IMULogFile.flush();
    delay(10);
    PWRLogFile = SD.open("PWR_LOG.txt", FILE_WRITE);
    PWRLogFile.println("DateTime,BatteryVoltage[V],CurrentConsumption[A]");
    PWRLogFile.flush();
    delay(10);
    ENVLogFile = SD.open("ENV_LOG.txt", FILE_WRITE);
    ENVLogFile.println(
            "DateTime,BMEPressure[hPa],BMETemp[degC],BMEHumidity[%],SSCPressure[PSI],SSCTemp[degC],BNOTemp[degC],MCPTemp[degC]");
    ENVLogFile.flush();
    delay(10);

    //// Init Xbee
    /* InitXbee() will configure the attached xbee so that it can talk to
     *   xbees which also use this library. It also handles the initalization
     *   of the adafruit xbee library
     */
    InitStat.xbeeStatus = ccsds_xbee.init(XBEE_ADDR, XBEE_PAN_ID,
    xbee_serial);
    if (!InitStat.xbeeStatus)
    {
        debug_serial.println(F("XBee Initialized!"));
    }
    else
    {
        debug_serial.print(F("XBee Failed to Initialize with Error Code: "));
        debug_serial.println(InitStat.xbeeStatus);
    }

    ccsds_xbee.add_rtc(rtc);
    ccsds_xbee.start_logging(xbeeLogFile);

    //// BNO
    InitStat.BNO_init = bno.begin();
    if (!InitStat.BNO_init)
    {
        debug_serial.println("BNO055 NOT detected.");
    }
    else
    {
        debug_serial.println("BNO055 detected!");
    }
    delay(500);
    bno.setExtCrystalUse(true);

    //// MCP9808
    InitStat.MCP_init = tempsensor.begin(0x18);
    if (!InitStat.MCP_init)
    {
        debug_serial.println("MCP9808 NOT detected.");
    }
    else
    {
        debug_serial.println("MCP9808 detected!");
    }

    //// Init BME
    // Temp/pressure/humidity sensor
    InitStat.BME_init = bme.begin(0x76);
    if (!InitStat.BME_init)
    {
        debug_serial.println("BME280 NOT detected.");
    }
    else
    {
        debug_serial.println("BME280 detected!");
    }

    //// Init SSC
    //  set min / max reading and pressure, see datasheet for the values for your
    //  sensor
    ssc.setMinRaw(0);
    ssc.setMaxRaw(16383);
    ssc.setMinPressure(0.0);
    ssc.setMaxPressure(30);
    //  start the sensor
    InitStat.SSC_init = ssc.start();
    if (!InitStat.SSC_init)
    {
        debug_serial.println("SSC started ");
    }
    else
    {
        debug_serial.println("SSC failed!");
    }

    //// Init ADS
    // ADC, used for current consumption/battery voltage
    ads.begin();
    ads.setGain(GAIN_ONE);
    debug_serial.println("Initialized ADS1015");

    //// set interface counters to zero
    CmdExeCtr = 0;
    CmdRejCtr = 0;
    XbeeRcvdByteCtr = 0;
    XbeeSentByteCtr = 0;

    // write entry in init log file
    print_time(initLogFile);
    initLogFile.print(", ");
    initLogFile.print(InitStat.rtc_start);
    initLogFile.print(", ");
    initLogFile.print(InitStat.rtc_running);
    initLogFile.print(", ");
    initLogFile.print(InitStat.BNO_init);
    initLogFile.print(", ");
    initLogFile.print(InitStat.BME_init);
    initLogFile.print(", ");
    initLogFile.print(InitStat.MCP_init);
    initLogFile.print(", ");
    initLogFile.print(InitStat.SSC_init);
    initLogFile.print(", ");
    initLogFile.print(InitStat.xbeeStatus);
    initLogFile.print(", ");
    initLogFile.print(InitStat.SD_detected);
    initLogFile.println();
    initLogFile.close();

    debug_serial.println(F("MARS Initialized"));

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
    /*  loop()
     *
     *  Reads sensor data if cycle counters indicate to
     *  Reads from xbee and processes any data
     *  retracts actuator if timeout exceeded
     */

    // declare structures to store data
    IMUData_s IMUData;
    PWRData_s PWRData;
    ENVData_s ENVData;

    // increment read counters
    imu_read_ctr++;
    pwr_read_ctr++;
    env_read_ctr++;

    // read sensors if time between last read
    if (imu_read_ctr > imu_read_lim)
    {
        read_imu(&IMUData);
        log_imu(IMUData, IMULogFile);
        imu_read_ctr = 0;
    }
    if (pwr_read_ctr > pwr_read_lim)
    {
        read_pwr(&PWRData);
        log_pwr(PWRData, PWRLogFile);
        pwr_read_ctr = 0;
    }
    if (env_read_ctr > env_read_lim)
    {
        read_env(&ENVData);
        log_env(ENVData, ENVLogFile);
        env_read_ctr = 0;
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
        command_response(ReadData, BytesRead, IMUData, ENVData, PWRData);
    }

    // if time on exceeds timeout seconds set in program constants, then retract actuator
    if (extended)
    {
        if (millis() > timeout_seconds * 1000)
        {
            debug_serial.println("Timeout exceeded.");
            retract(10);
            extended = false;
        }
    }

    // wait a bit
    delay(CYCLE_DELAY);
}

void command_response(uint8_t data[], uint8_t data_len,
        struct IMUData_s IMUData, struct ENVData_s ENVData,
        struct PWRData_s PWRData)
{
    /*  command_response()
     *
     *  given an array of data (presumably containing a CCSDS packet), check if:
     *    the packet is a command packet
     *    the APID is the LINK command packet APID
     *    the checksum in the header is correct
     *  if so, process it
     *  otherwise, reject it
     */

    // get the APID (the field which identifies the type of packet)
    uint16_t _APID = getAPID(data);

    if (_APID != COMMAND_APID)
    {
        debug_serial.print("Unrecognized apid 0x");
        debug_serial.println(_APID, HEX);
        return;
    }
    if (!getPacketType(data))
    {
        debug_serial.print("Not a command packet");
        return;
    }

    // validate command checksum
    /*if (!validateChecksum(data))
    {
        Serial.println("Command checksum doesn't validate");
        CmdRejCtr++;
        return;
     }*/

    uint8_t destAddr = 0;
    uint16_t pktLength = 0;
    uint8_t payloadLength = 0;
    uint8_t Pkt_Buff[100];
    uint8_t payload_buff[100];

    // respond to the command depending on what type of command it is
    switch (getCmdFunctionCode(data))
    {

        // NoOp Cmd
        case COMMAND_NOOP:
        {
            // No action other than to increment the interface counters

            debug_serial.println("Received NoOp Cmd");

            // increment the cmd executed counter
            CmdExeCtr++;
            break;
        }
            // REQ_Name Cmd
        case REQUEST_PAYLOAD_NAME:
        {
            /*
             * This command requests that a packet containing the payload's
             * name be sent to a specific xbee. The format of the command is:
             *   CCSDS Command Header (8 bytes)
             *   Xbee address (uint8_t)
             * There is one parameter associated with this command, the address
             * of the xbee to send the Name message to. The format of the Name message
             * which is sent out is:
             *   CCSDS Telemetry Header (12 bytes)
             *   Payload Name (string, 8 bytes)
             */
            debug_serial.print("Received Name Cmd to addr ");

            // define variables to process the command
            uint8_t NamedestAddr = 0;
            uint16_t pktLength = 0;
            uint8_t payloadLength = 0;
            int success_flg = 0;

            // define buffer to create the response in
            uint8_t Name_Payload_Buff[PKT_MAX_LEN];

            // extract the desintation address from the command
            extractFromTlm(NamedestAddr, data, 8);
            debug_serial.println(NamedestAddr);

            // Use sprintf to pad/trim the string if the payload name isn't
            // exactly 8 characters
            char payloadname[8];
            sprintf(payloadname, "%8.8s", PAYLOAD_NAME);

            // print the name to debug
            debug_serial.println(payloadname);

            // add the information to the buffer
            payloadLength = addStrToTlm(payloadname, Name_Payload_Buff,
                    payloadLength);

            // send the telemetry message by adding the buffer to the header
            success_flg = ccsds_xbee.sendTlmMsg(NamedestAddr, PAYLOAD_NAME_APID,
                    Name_Payload_Buff, payloadLength);

            if (success_flg > 0)
            {
                // increment the cmd executed counter
                CmdExeCtr++;
            }
            else
            {
                CmdRejCtr++;
            }
            break;
        }
            // REQ_HK
        case REQUEST_PACKET_COUNTERS:
        {
            // Requests that an HK packet be sent to the ground
            debug_serial.print("Received HKReq Cmd to addr ");
            /*  Command format:
             *   CCSDS Command Header (8 bytes)
             *   Xbee address (1 byte) (or 0 if Gnd)
             */
            uint8_t destAddr = 0;
            uint16_t pktLength = 0;
            uint8_t payloadLength = 0;

            // extract the desintation address from the command
            extractFromTlm(destAddr, data, 8);
            debug_serial.println(destAddr);

            // create a HK pkt
            payloadLength = create_HK_payload(payload_buff);

            pktLength = ccsds_xbee.createTlmMsg(Pkt_Buff, PACKET_COUNTER_APID,
                    payload_buff, payloadLength);
            if (pktLength > 0)
            {

                // send the data
                send_and_log(destAddr, Pkt_Buff, pktLength);
            }

            // increment the cmd executed counter
            CmdExeCtr++;
            break;
        }
        case COMMAND_CLEAR_PACKET_COUNTERS:
        {
            // Requests that an HK packet be sent to the specified xbee address
            /*  Command format:
             *   CCSDS Command Header (8 bytes)
             *   Xbee address (1 byte)
             */

            debug_serial.println("Received ResetCtr Cmd");

            break;
        }
        case COMMAND_REBOOT:
        {
            // Requests reboot
            debug_serial.println("Received Reboot Cmd");

            // set the reboot timer
            wdt_enable (WDTO_1S);

            break;
        }
        case COMMAND_DISARM:
        {
            debug_serial.println("received disarm command");

            // extract the desintation address from the command
            extractFromTlm(destAddr, data, 8);

            // create a pkt
            pktLength = create_Status_pkt(Pkt_Buff, DISARM_RESPONSE);

            // send the HK packet via xbee and log it
            send_and_log(destAddr, Pkt_Buff, pktLength);

            disarm();

            break;
        }
        case COMMAND_KEEPALIVE:
        {
            debug_serial.println("Received keepalive packet");

            // extract the desintation address from the command
            extractFromTlm(destAddr, data, 8);

            // create a pkt
            pktLength = create_Status_pkt(Pkt_Buff, KEEPALIVE_RESPONSE);

            // send the HK packet via xbee and log it
            send_and_log(destAddr, Pkt_Buff, pktLength);

            break;
        }
        case COMMAND_EXTEND_ACTUATOR:
        {
            debug_serial.println("Received extend actuator command");

            // extract the desintation address from the command
            extractFromTlm(destAddr, data, 8);

            // create a pkt
            pktLength = create_Status_pkt(Pkt_Buff, EXTEND_RESPONSE);

            // send the HK packet via xbee and log it
            send_and_log(destAddr, Pkt_Buff, pktLength);

            extend(6);

            break;
        }
        case COMMAND_RETRACT_ACTUATOR:
        {
            debug_serial.println("Received retract actuator command");

            // extract the desintation address from the command
            extractFromTlm(destAddr, data, 8);

            // create a pkt
            pktLength = create_Status_pkt(Pkt_Buff, RETRACT_RESPONSE);

            // send the HK packet via xbee and log it
            send_and_log(destAddr, Pkt_Buff, pktLength);

            retract(30);

            break;
        }
        case REQUEST_ACTUATOR_STATUS:
        {
            debug_serial.println("Received actuator status request");

            // extract the desintation address from the command
            extractFromTlm(destAddr, data, 8);

            // create a pkt
            pktLength = create_Status_pkt(Pkt_Buff,
                    (extended) ? EXTEND_RESPONSE : RETRACT_RESPONSE);

            // send the HK packet via xbee and log it
            send_and_log(destAddr, Pkt_Buff, pktLength);

            break;
        }
            // unrecognized fcn code
        default:
        {
            debug_serial.print("unrecognized fcn code ");
            debug_serial.println(getCmdFunctionCode(data), HEX);

            // reject command
            CmdRejCtr++;
        }
    } // end switch(FcnCode)

} // end command_response()

void send_and_log(uint8_t dest_addr, uint8_t data[], uint8_t data_len)
{
    /*  send_and_log()
     *
     *  If the destination address is 0, calls radio_send_and_log. Otherwise
     *  calls send_and_log
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

void print_time(File file)
{
    /*  print_time()
     *
     *  Prints the current time to the given log file
     */

    // get the current time from the RTC
    DateTime now = SoftRTC.now();
    uint32_t nowMS = millis();

    // print a datestamp to the file
    char buf[50];
    sprintf(buf, "%02d/%02d/%02d %02d:%02d:%02d.%03d", now.day(), now.month(),
            now.year(), now.hour(), now.minute(), now.second(),
            (nowMS - start_millis) % 1000);  // print milliseconds);
    file.print(buf);
}

void log_imu(struct IMUData_s IMUData, File IMULogFile)
{
    /*  log_imu()
     *
     *  Writes the IMU data to a log file with a timestamp.
     *
     */

    // print the time to the file
    print_time(IMULogFile);

    // print the sensor values
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.system_cal);
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.accel_cal);
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.gyro_cal);
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.mag_cal);
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.accel_x);
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.accel_y);
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.accel_z);
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.gyro_x);
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.gyro_y);
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.gyro_z);
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.mag_x);
    IMULogFile.print(", ");
    IMULogFile.print(IMUData.mag_y);
    IMULogFile.print(", ");
    IMULogFile.println(IMUData.mag_z);

    IMULogFile.flush();
}

void log_env(struct ENVData_s ENVData, File ENVLogFile)
{
    /*  log_env()
     *
     *  Writes the ENV data to a log file with a timestamp.
     *
     */

    // print the time to the file
    print_time(ENVLogFile);

    // print the sensor values
    ENVLogFile.print(", ");
    ENVLogFile.print(ENVData.bme_pres);
    ENVLogFile.print(", ");
    ENVLogFile.print(ENVData.bme_temp);
    ENVLogFile.print(", ");
    ENVLogFile.print(ENVData.bme_humid);
    ENVLogFile.print(", ");
    ENVLogFile.print(ENVData.ssc_pres);
    ENVLogFile.print(", ");
    ENVLogFile.print(ENVData.ssc_temp);
    ENVLogFile.print(", ");
    ENVLogFile.print(ENVData.bno_temp);
    ENVLogFile.print(", ");
    ENVLogFile.println(ENVData.mcp_temp);

    ENVLogFile.flush();
}

void log_pwr(struct PWRData_s PWRData, File PWRLogFile)
{
    /*  log_pwr()
     *
     *  Writes the PWR data to a log file with a timestamp.
     *
     */

    // print the time to the file
    print_time(PWRLogFile);

    // print the sensor values
    PWRLogFile.print(", ");
    PWRLogFile.print(PWRData.batt_volt, 4);
    PWRLogFile.print(", ");
    PWRLogFile.println(PWRData.i_consump, 4);

    PWRLogFile.flush();
}

void read_env(struct ENVData_s *ENVData)
{
    /*  read_env()
     *
     *  Reads all of the environmental sensors and stores data in
     *  a structure.
     *
     */

    //BME280
    ENVData->bme_pres = bme.readPressure() / 100.0F; // hPa
    ENVData->bme_temp = bme.readTemperature(); // degC
    ENVData->bme_humid = bme.readHumidity(); // %
    /*
     * This is causing LINK to not respond to commands... not sure why
     //  SSC
     ssc.update();
     ENVData->ssc_pres = ssc.pressure(); // PSI
     ENVData->ssc_temp = ssc.temperature(); // degC
     */
    // BNO
    ENVData->bno_temp = bno.getTemp();

    //MCP9808
    ENVData->mcp_temp = tempsensor.readTempC(); // degC
}

void read_pwr(struct PWRData_s *PWRData)
{
    /*  read_pwr()
     *
     *  Reads all of the power sensors and stores data in
     *  a structure.
     *
     */
    PWRData->batt_volt = ((float) ads.readADC_SingleEnded(2)) * 0.002 * 3.0606; // V
    PWRData->i_consump = (((float) ads.readADC_SingleEnded(3)) * 0.002 - 2.5)
            * 10;
}

void read_imu(struct IMUData_s *IMUData)
{
    /*  read_imu()
     *
     *  Reads all of the IMU sensors and stores data in
     *  a structure.
     *
     */
    uint8_t system_cal, gyro_cal, accel_cal, mag_cal = 0;
    bno.getCalibration(&system_cal, &gyro_cal, &accel_cal, &mag_cal);

    // get measurements
    imu::Vector < 3 > mag = bno.getVector(Adafruit_BNO055::VECTOR_MAGNETOMETER); // (values in uT, micro Teslas)
    imu::Vector < 3 > gyro = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE); // (values in rps, radians per second)
    imu::Vector < 3 > accel = bno.getVector(
            Adafruit_BNO055::VECTOR_ACCELEROMETER); // (values in m/s^2)

    // assign them into global variables
    IMUData->system_cal = system_cal;
    IMUData->accel_cal = accel_cal;
    IMUData->gyro_cal = gyro_cal;
    IMUData->mag_cal = mag_cal;
    IMUData->accel_x = accel.x();
    IMUData->accel_y = accel.y();
    IMUData->accel_z = accel.z();
    IMUData->gyro_x = gyro.x();
    IMUData->gyro_y = gyro.y();
    IMUData->gyro_z = gyro.z();
    IMUData->mag_x = mag.x();
    IMUData->mag_y = mag.y();
    IMUData->mag_z = mag.z();

}

uint16_t create_HK_payload(uint8_t Pkt_Buff[])
{
    /*  create_HK_pkt()
     *
     *  Creates an HK packet containing the values of all the interface counters.
     *  Packet data is filled into the memory passed in as the argument. This function
     *  assumes that the buffer is large enough to hold this packet.
     *
     */

    // initalize counter to record length of packet
    uint16_t payloadSize = 0;

    // Add counter values to the pkt
    payloadSize = addIntToTlm(CmdExeCtr, Pkt_Buff, payloadSize); // Add counter of sent packets to message
    payloadSize = addIntToTlm(CmdRejCtr, Pkt_Buff, payloadSize); // Add counter of sent packets to message
    payloadSize = addIntToTlm(ccsds_xbee.getRcvdByteCtr(), Pkt_Buff,
            payloadSize); // Add counter of sent packets to message
    payloadSize = addIntToTlm(ccsds_xbee.getSentByteCtr(), Pkt_Buff,
            payloadSize); // Add counter of sent packets to message
    payloadSize = addIntToTlm(millis() / 1000L, Pkt_Buff, payloadSize); // Timer

    return payloadSize;
}

uint16_t create_ENV_payload(uint8_t payload[], struct ENVData_s ENVData)
{
    /*  create_ENV_pkt()
     *
     *  Creates an ENV packet containing the values of all environmental sensors.
     *  Packet data is filled into the memory passed in as the argument. This function
     *  assumes that the buffer is large enough to hold this packet.
     *
     */

    uint16_t payloadSize = 0;
    // Add counter values to the pkt
    payloadSize = addFloatToTlm(ENVData.bme_pres, payload, payloadSize); // Add bme pressure to message [Float]
    payloadSize = addFloatToTlm(ENVData.bme_temp, payload, payloadSize); // Add bme temperature to message [Float]
    payloadSize = addFloatToTlm(ENVData.bme_humid, payload, payloadSize); // Add bme humidity to message [Float]
    payloadSize = addFloatToTlm(ENVData.ssc_pres, payload, payloadSize); // Add ssc pressure to message [Float]
    payloadSize = addFloatToTlm(ENVData.ssc_temp, payload, payloadSize); // Add ssc temperature to messsage [Float]
    payloadSize = addFloatToTlm(ENVData.bno_temp, payload, payloadSize); // Add bno temperature to message [Float]
    payloadSize = addFloatToTlm(ENVData.mcp_temp, payload, payloadSize); // Add mcp temperature to message [Float]

    return payloadSize;

}

uint16_t create_PWR_payload(uint8_t Pkt_Buff[], struct PWRData_s PWRData)
{
    /*  create_PWR_pkt()
     *
     *  Creates an PWR packet containing the values of all the power/battery sensors.
     *  Packet data is filled into the memory passed in as the argument. This function
     *  assumes that the buffer is large enough to hold this packet.
     *
     */

    // initalize counter to record length of packet
    uint16_t payloadSize = 0;

    // Add counter values to the pkt
    payloadSize = addFloatToTlm(PWRData.batt_volt, Pkt_Buff, payloadSize); // Add battery voltage to message [Float]
    payloadSize = addFloatToTlm(PWRData.i_consump, Pkt_Buff, payloadSize); // Add current consumption to message [Float]

    return payloadSize;

}

uint16_t create_IMU_payload(uint8_t Pkt_Buff[], struct IMUData_s IMUData)
{
    /*  create_IMU_pkt()
     *
     *  Creates an IMU packet containing the values of all the IMU sensors.
     *  Packet data is filled into the memory passed in as the argument. This function
     *  assumes that the buffer is large enough to hold this packet.
     *
     */

    // initalize counter to record length of packet
    uint16_t payloadSize = 0;

    // Add counter values to the pkt
    payloadSize = addIntToTlm(IMUData.system_cal, Pkt_Buff, payloadSize); // Add system cal status to message [uint8_t]
    payloadSize = addIntToTlm(IMUData.accel_cal, Pkt_Buff, payloadSize); // Add accelerometer cal status to message [uint8_t]
    payloadSize = addIntToTlm(IMUData.gyro_cal, Pkt_Buff, payloadSize); // Add gyro cal status to message [uint8_t]
    payloadSize = addIntToTlm(IMUData.mag_cal, Pkt_Buff, payloadSize); // Add mnagnetomter cal status to message [uint8_t]
    payloadSize = addFloatToTlm(IMUData.accel_x, Pkt_Buff, payloadSize); // Add battery accelerometer x to message [Float]
    payloadSize = addFloatToTlm(IMUData.accel_y, Pkt_Buff, payloadSize); // Add battery accelerometer y to message [Float]
    payloadSize = addFloatToTlm(IMUData.accel_z, Pkt_Buff, payloadSize); // Add battery accelerometer z to message [Float]
    payloadSize = addFloatToTlm(IMUData.gyro_x, Pkt_Buff, payloadSize); // Add battery accelerometer x to message [Float]
    payloadSize = addFloatToTlm(IMUData.gyro_y, Pkt_Buff, payloadSize); // Add battery accelerometer y to message [Float]
    payloadSize = addFloatToTlm(IMUData.gyro_z, Pkt_Buff, payloadSize); // Add battery accelerometer z to message [Float]
    payloadSize = addFloatToTlm(IMUData.mag_x, Pkt_Buff, payloadSize); // Add battery accelerometer x to message [Float]
    payloadSize = addFloatToTlm(IMUData.mag_y, Pkt_Buff, payloadSize); // Add battery accelerometer y to message [Float]
    payloadSize = addFloatToTlm(IMUData.mag_z, Pkt_Buff, payloadSize); // Add battery accelerometer z to message [Float]

    return payloadSize;

}

uint16_t create_INIT_payload(uint8_t Pkt_Buff[], struct InitStat_s InitStat)
{
    /*  create_IMU_pkt()
     *
     *  Creates an IMU packet containing the values of all the IMU sensors.
     *  Packet data is filled into the memory passed in as the argument. This function
     *  assumes that the buffer is large enough to hold this packet.
     *
     */

    // initalize counter to record length of packet
    uint16_t payloadSize = 0;

    // Add counter values to the pkt
    payloadSize = addIntToTlm(InitStat.xbeeStatus, Pkt_Buff, payloadSize); // Add system cal status to message [uint8_t]
    payloadSize = addIntToTlm(InitStat.rtc_running, Pkt_Buff, payloadSize); // Add accelerometer cal status to message [uint8_t]
    payloadSize = addIntToTlm(InitStat.rtc_start, Pkt_Buff, payloadSize); // Add gyro cal status to message [uint8_t]
    payloadSize = addIntToTlm(InitStat.BNO_init, Pkt_Buff, payloadSize); // Add mnagnetomter cal status to message [uint8_t]
    payloadSize = addIntToTlm(InitStat.MCP_init, Pkt_Buff, payloadSize); // Add mnagnetomter cal status to message [uint8_t]
    payloadSize = addIntToTlm(InitStat.BME_init, Pkt_Buff, payloadSize); // Add mnagnetomter cal status to message [uint8_t]
    payloadSize = addIntToTlm(InitStat.SSC_init, Pkt_Buff, payloadSize); // Add mnagnetomter cal status to message [uint8_t]
    payloadSize = addIntToTlm(InitStat.SD_detected, Pkt_Buff, payloadSize); // Add mnagnetomter cal status to message [uint8_t]

    return payloadSize;

}

// MARS-specific functions

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
