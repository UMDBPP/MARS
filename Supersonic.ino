/*
 * Supersonic.ino
 * electromagnet is on pin 13 (Arduino Uno)
 */

#include <BalloonModule.h>
BalloonModule module;
double altitude;
const double releaseAltitude = 22860;    // in meters
const int electromagnetPin = 13;    // Pin 13 has the electromagnet attached to it
const int minReleaseTime = 30 * 60;    // Minimum time in seconds before release is allowed to occur (30 minutes)
const int maxReleaseTime = 5 * 3600;    // Maximum time in seconds before release will occur (5 hours)
const int releaseTolerance = 5;    // time in seconds of sensor value above release altitude after which to detach
int release = 0;
bool isLaunched = false;

void setup()
{
    // main initialization of pressure sensor
    module.initialize();
    
    // print extra module-specific information
    printTime();
    Serial.print("EM cutoff will occur at ");
    module.printMetersAndFeet(releaseAltitude);
    Serial.print(" or after max time of ");
    Serial.print(maxReleaseTime / 3600);
    Serial.print(" hours.");
    Serial.println();
    
    // initialize the electromagnet pin (digital) as output.
    pinMode(electromagnetPin, OUTPUT);
}

void loop()
{
    // use PrintStatusAfterLaunch to stop altitude output until launch happens (20 meters and above over baseline)
    altitude = module.printStatusAfterLaunch();
    
    // Set electromagnet to LOW if altitude has not yet been reached
    if (altitude < releaseAltitude || MET < minReleaseTime)
    {
        digitalWrite(electromagnetPin, LOW);    // set the electromagnet to off (LOW is the voltage level)
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
    else if (MET > maxReleaseTime)
    {
        releaseEMCutdown("max time exceeded");
    }
}

// Releases module and prints verification with given cause for release
void releaseEMCutdown(String cause)
{
    digitalWrite(electromagnetPin, HIGH);
    printTime();
    Serial.print(
            "MODULE RELEASED (triggered by " + cause + ")." + "\n"
                    + "System going to sleep until recovery.");
    module.printAltitude();
    Serial.println();
    while (1)
    {
        // infinite loop to pause forever
    }
}

