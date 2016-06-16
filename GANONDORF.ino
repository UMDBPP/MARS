/*
 * GANONDORF.ino
 * electromagnet is on pin 13
 * pressure sensor pins:
 * green wire -> SDA
 * blue wire -> SCL
 * white wire -> 3.3v
 * black wire -> GND
 */

#include <Arduino.h>
#include <BMP180.h>

const double releaseAltitude = 36575;    // in meters
const int electromagnetPin = 8;    // Pin 13 has the electromagnet attached to it
const int minReleaseTime = 30 * 60;    // Minimum time in seconds before release is allowed to occur (30 minutes)
const int maxReleaseTime = 5 * 3600;    // Maximum time in seconds before release will occur (5 hours)
const int releaseTolerance = 5;    // time in seconds of sensor value above release altitude after which to detach
double altitude = 0;
int release = 0;
char status;

BMP180 pressureSensor;

void setup()
{
    Serial.begin(9600);

    // initialize the electromagnet pin (digital) as output.
    pinMode(electromagnetPin, OUTPUT);
    digitalWrite(electromagnetPin, LOW);

    status = pressureSensor.begin();

    if (status != 0)
    {
        // print extra module-specific information
        pressureSensor.print(
                "Release will occur at " + String(releaseAltitude)
                        + "m or after max time of "
                        + String(maxReleaseTime / 3600) + " hours.");
    }
}

// Releases module and prints verification with given cause for release
void releaseCutdown(String cause)
{
    // attempt 3 times
    for (int count = 0; count < 3; count++)
    {
        digitalWrite(electromagnetPin, HIGH);
        delay(2500);
        digitalWrite(electromagnetPin, LOW);
        delay(2500);
    }
    pressureSensor.print(
            "MODULE RELEASED (triggered by " + cause + ") at "
                    + String(altitude) + "m.");
}

void loop()
{
    if (status != 0)
    {
        altitude = pressureSensor.getAltitude();

        if ( (millis() / 1000) > maxReleaseTime)
        {
            releaseCutdown("max time exceeded");
        }
        else if (altitude > releaseAltitude
                && (millis() / 1000) > minReleaseTime)
        {
            releaseCutdown("altitude value");
        }
    }
    else
    {
        status = pressureSensor.begin();
    }
    delay(1000);
}
