/*
 * Cutdown.ino
 * electromagnet is on pin 13
 * pressure sensor pins:
 * green wire -> SDA
 * blue wire -> SCL
 * white wire -> 3.3v
 * black wire -> GND
 */

#include <Arduino.h>
#include <Balloonduino.h>

Balloonduino module;

const double releaseAltitude = 36575;    // in meters
const int electromagnetPin = 13;    // Pin 13 has the electromagnet attached to it
const int minReleaseTime = 30 * 60;    // Minimum time in seconds before release is allowed to occur (30 minutes)
const int maxReleaseTime = 5 * 3600;    // Maximum time in seconds before release will occur (5 hours)
const int releaseTolerance = 5;    // time in seconds of sensor value above release altitude after which to detach
double altitude = 0;
int release = 0;

void setup()
{
    Serial.begin(9600);
    // print extra module-specific information
    Serial.println();
    module.printFormattedTime();
    Serial.print("Release will occur at ");
    module.printMetersAndFeet(releaseAltitude);
    Serial.print(" or after max time of ");
    Serial.print(maxReleaseTime / 3600);
    Serial.print(" hours.");
    Serial.println();

    // initialize the electromagnet pin (digital) as output.
    pinMode(electromagnetPin, OUTPUT);
    pinMode(electromagnetPin, LOW);
}

void loop()
{
    // stop altitude output until launch happens (above 20 meters over baseline)
    altitude = module.getAltitude();
    module.printStatusDuringFlight(0);

    // Set cutdown to hold if altitude has not yet been reached
    if (altitude < releaseAltitude || (millis() / 1000) < minReleaseTime)
    {
        release = 0;
    }
    else
    {
        release++;    // constitutes one loop (one second) of being at or above release altitude
    }

    // Release electromagnet if altitude trigger or max time
    if (release > 5)
    {
        releaseEMCutdown("pressure sensor value");
    }
    else if ((millis() / 1000) > maxReleaseTime)
    {
        releaseEMCutdown("max time exceeded");
    }
}

// Releases module and prints verification with given cause for release
void releaseEMCutdown(String cause)
{
    digitalWrite(electromagnetPin, HIGH);
    module.printFormattedTime();
    Serial.print(
            "MODULE RELEASED (triggered by " + cause + ")." + "\n"
                    + "System going to sleep until recovery.");
    module.printAltitude(altitude);
    Serial.println();
    while (1)
    {
        // infinite loop to pause forever
    }
}

