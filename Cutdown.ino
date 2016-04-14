/*
 * Cutdown.ino
 * electromagnet is on pin 13
 * pressure sensor pins:
 * green wire -> SDA
 * blue wire -> SCL
 * white wire -> 3.3v
 * black wire -> GND
 */

const double releaseAltitude = 36575;    // in meters
const int electromagnetPin = 13;  // Pin 13 has the electromagnet attached to it
const int minReleaseTime = 30 * 60; // Minimum time in seconds before release is allowed to occur (30 minutes)
const int maxReleaseTime = 5 * 3600; // Maximum time in seconds before release will occur (5 hours)
const int releaseTolerance = 5; // time in seconds of sensor value above release altitude after which to detach
double altitude = 0;
int release = 0;

void setup()
{
    Serial.begin(9600);

    // initialize the electromagnet pin (digital) as output.
    pinMode(electromagnetPin, OUTPUT);
    digitalWrite(electromagnetPin, LOW);

    // print extra module-specific information
    Serial.println(
            getMissionTimeString() + "Release will occur at "
                    + String(releaseAltitude) + "m or after max time of "
                    + String(maxReleaseTime / 3600) + " hours.");
}

void loop()
{
    // stop altitude output until launch happens (above 20 meters over baseline)
    altitude = getAltitude();

    // Set cutdown to hold if altitude has not yet been reached
    if (altitude < releaseAltitude || (millis() / 1000) < minReleaseTime)
    {
        release = 0;
    }
    else
    {
        release++; // constitutes one loop (one second) of being at or above release altitude
    }

    // Release electromagnet if altitude trigger or max time
    if (release > 5)
    {
        releaseCutdown("altitude value");
    }
    else if ((millis() / 1000) > maxReleaseTime)
    {
        releaseCutdown("max time exceeded");
    }
}

// Releases module and prints verification with given cause for release
void releaseCutdown(String cause)
{
    // attempt 3 times
    for (int count = 0; count < 3; count++)
    {
        digitalWrite(electromagnetPin, HIGH);
        Serial.println(
                getMissionTimeString() + "MODULE RELEASED (triggered by "
                        + cause + ") at " + String(altitude) + "m.");
        delay(2500);
        digitalWrite(electromagnetPin, LOW);
        delay(2500);
    }
}

// Returns current millisecond time string as "T+HH:MM:SS"
String getMissionTimeString()
{
    byte seconds = millis() % 60, minutes = millis() / 60;
    byte hours = minutes / 60;

    String out = "T+";
    if (hours < 10)
    {
        out += "0";
    }
    out += String(hours) + ":";
    if (minutes < 10)
    {
        out += "0";
    }
    out += String(minutes) + ":";
    if (seconds < 10)
    {
        out += "0";
    }
    out += String(seconds);
    return out;
}

double getAltitude()
{
    return 0;
}

