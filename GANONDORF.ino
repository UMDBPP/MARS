/*
 * GANONDORF.ino
 * Linear actuator releases a loop of string on command
 */

#include <Arduino.h>
#include <Cutdown.h>

#define ACTUATOR_PIN 8   // actuator pin
#define XBEE_ADDR 04     // XBee channel

const int maxReleaseSeconds = 0.5 * 3600;    // Release timeout override
const int pulseSeconds = 60;         // Seconds for the actuator to run
char status;

Cutdown cutdown;

void setup()
{
    Serial.begin(9600);

    // initialize the trigger pin (digital) as output and set to low
    pinMode(ACTUATOR_PIN, OUTPUT);
    digitalWrite(ACTUATOR_PIN, LOW);

    // initialize cutdown library
    status = cutdown.begin();

    if (status != 0)
    {
        // print extra module-specific information
        log("MODULE INITIALIZED: cutdown will release in " + String(maxReleaseSeconds) + " seconds");
    }
}

void loop()
{
    cutdown.check_input();

    if (cutdown.cutdown_is_released())
    {
        fire();
    }
}

void fire()
{
    // if the system is armed, release cutdown
    if (cutdown.system_is_armed())
    {
        // disarm the system to prevent repeated firing
        cutdown.disarm_system();

        digitalWrite(ACTUATOR_PIN, HIGH);
        delay(pulseSeconds * 1000);
        digitalWrite(ACTUATOR_PIN, LOW);

        cutdown.send_release_confirmation();

        log("MODULE RELEASED");
    }
}

void log(String message)
{
    byte seconds = (millis() / 1000) % 60;
    byte minutes = seconds / 60;
    byte hours = minutes / 60;

    String time_string = "T+";
    if (hours < 10)
    {
        time_string += "0";
    }
    time_string += String(hours) + ":";
    if (minutes < 10)
    {
        time_string += "0";
    }
    time_string += String(minutes) + ":";
    if (seconds < 10)
    {
        time_string += "0";
    }
    time_string += String(seconds);

    Serial.println("[" + time_string + "] " + message);
}
