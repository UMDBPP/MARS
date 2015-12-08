/*
 * green -> SDA, blue -> SCL, white -> 3.3v, black -> GND
 * 
 * electromagnet is on pin 13
 */

#include <SFE_BMP180.h>
#include <Wire.h>
SFE_BMP180 pressureSensor;
double temperature, altitude, pressure, baselinePressure;
const double resumeAltitude = 20000, releaseAltitude = 22860;    // in meters
unsigned long millisecondTime = 0;
float MET, hours = 0, minutes = 0, seconds = 0;
int hoursInt = 0, minutesInt = 0, secondsInt = 0;
const int electromagnetPin = 13;    // Pin 13 has the electromagnet attached to it
const int minReleaseTime = 30 * 60;    // Minimum time in seconds before release is allowed to occur (30 minutes)
const int maxReleaseTime = 5 * 3600;    // Maximum time in seconds before release will occur (5 hours)
const int releaseTolerance = 5;    // time in seconds of sensor value above release altitude after which to detach
const int delayMilliseconds = 1000;    // time in milliseconds to delay (wait) each repetition
int release = 0, launchTolerance = 0;
bool isLaunched = false;

void setup()
{
    // initialize the electromagnet pin (digital) as output.
    pinMode(electromagnetPin, OUTPUT);
    Serial.begin(9600);
    
    printTime();
    Serial.println("Initializing...");
    
    if (pressureSensor.begin())
    {
        printTime();
        if (pressureSensor.begin())
        {
            Serial.println("Pressure sensor successfully initialized.");
            printTime();
            Serial.println("Now attempting baseline pressure reading...");
        }
        else
        {
            Serial.println(
                    "Pressure sensor initialization failed (is it disconnected?)");
            printTime();
            Serial.println("System going to sleep.");
            while (1)
            {
                // infinite loop to pause forever
            }
        }
        
        delay(1000);
        
        // Get baseline pressure
        baselinePressure = getPressure();
        
        // Print baseline pressure
        printTime();
        printValue("Baseline pressure is ", baselinePressure, "mb.");
        Serial.println();
        printTime();
        printMeters("Output will resume at ", resumeAltitude);
        printMeters(". EM cutoff will occur at ", releaseAltitude);
        printValue(" or after max time of", maxReleaseTime / 3600, "hours");
        Serial.println();
        printTime();
        Serial.println("System is ready for launch.");
    }
    delay(3000);
}

void loop()
{
    // 1: Get a new pressure reading
    pressure = getPressure();
    
    // 2: Get the relative altitude difference between the new reading and the baseline reading
    altitude = pressureSensor.getAltitude(pressure, baselinePressure);
    
    // 3: Set electromagnet to LOW if altitude has not yet been reached
    if (altitude < releaseAltitude || MET < minReleaseTime)
    {
        digitalWrite(electromagnetPin, LOW);    // set the electromagnet to off (LOW is the voltage level)
        release = 0;
    }
    else
    {
        release++;    // constitutes one second of being at or above release altitude
    }
    
    // 5: Release electromagnet if altitude trigger or max time
    if (release > 5)
    {
        releaseModule("pressure sensor value");
    }
    else if (MET > maxReleaseTime)
    {
        releaseModule("max time exceeded");
    }
    
    if (altitude > 20.0)
    {
        launchTolerance++;
    }
    else
    {
        launchTolerance = 0;
    }
    if (launchTolerance > 5)
    {
        isLaunched = true;
        printTime();
        Serial.println("Specified altitude reached. Resuming output.");
    }
    
    if (isLaunched)
    {
        // 6: Print current time
        printTime();
        
        // 7: Print relative altitude (above baseline)
        printMeters("Altitude is ", altitude);
        Serial.println(" above launch site. ");
    }
    delay(delayMilliseconds);
}

// functions:

/*
 * Releases module and prints verification with given cause for release
 */
void releaseModule(String cause)
{
    digitalWrite(electromagnetPin, HIGH);
    printTime();
    Serial.println(
            "MODULE RELEASED (triggered by " + cause + ")." + "\n"
                    + "System going to sleep.");
    while (1)
    {
        // infinite loop to pause forever
    }
}

/*
 * Prints time in [HH:MM:SS] and returns current time in seconds
 * @returns current time in seconds
 */
void printTime()
{
    MET = (double) millis() / 1000.0;    // convert from milliseconds to seconds
    seconds = (int) MET % 60;
    minutes = MET / 60;
    hours = minutes / 60;
    Serial.print("[");
    if (hours < 10)
    {
        Serial.print("0");
    }
    Serial.print((int) hours);
    Serial.print(":");
    if (minutes < 10)
    {
        Serial.print("0");
    }
    Serial.print((int) minutes);
    Serial.print(":");
    if (seconds < 10)
    {
        Serial.print("0");
    }
    Serial.print((int) seconds);
    Serial.print("] ");
}

/*
 * Prints a value in format "Description: value units"
 */
void printValue(String description, double value, String units)
{
    Serial.print(description);
    Serial.print(value);
    Serial.print(" " + units);
}

/*
 * Prints meters and feet equivalent
 */
void printMeters(String description, double value)
{
    printValue(description, altitude, " meters ");
    printValue("(", altitude * 3.28084, " feet)");
}

/*
 * Returns current pressure reading.
 * @returns pressure
 */
double getPressure()
{
    char status;
    
    // You must first get a temperature measurement to perform a pressure reading.
    
    /*
     * Start a temperature measurement:
     * If request is successful, the number of ms to wait is returned.
     * If request is unsuccessful, 0 is returned.
     */
    status = pressureSensor.startTemperature();
    
    if (status != 0)
    {
        // Wait for the measurement to complete:
        delay(status);
        
        /*
         * Retrieve the completed temperature measurement:
         * Note that the measurement is stored in the variable 'temperature'.
         * Use '&temperature' to provide the address of temperature to the function.
         * Function returns 1 if successful, 0 if failure.
         */
        status = pressureSensor.getTemperature(temperature);
        
        if (status != 0)
        {
            /*
             * Start a pressure measurement:
             * The parameter is the oversampling setting, from 0 to 3 (highest res, longest wait).
             * If request is successful, the number of ms to wait is returned.
             * If request is unsuccessful, 0 is returned.
             */
            status = pressureSensor.startPressure(3);
            
            if (status != 0)
            {
                // Wait for the measurement to complete:
                delay(status);
                
                /*
                 * Retrieve the completed pressure measurement:
                 * Note that the measurement is stored in the variable 'pressure'.
                 * Use '&pressure' to provide the address of pressure.
                 * Note also that the function requires the previous temperature measurement.
                 * (If temperature is stable, you can do one temperature measurement for a number of pressure measurements.)
                 * Function returns 1 if successful, 0 if failure.
                 */
                status = pressureSensor.getPressure(pressure, temperature);
                
                if (status != 0)
                {
                    return (pressure);
                }
                else
                {
                    Serial.println("Error retrieving pressure measurement.");
                    return 0.0;
                }
            }
            else
            {
                Serial.println("Error starting pressure measurement.");
                return 0.0;
            }
        }
        else
        {
            Serial.println(
                    "Error retrieving required temperature measurement.");
            return 0.0;
        }
    }
    else
    {
        Serial.println("Error starting required temperature measurement.");
        return 0.0;
    }
}
