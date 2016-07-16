/*
 * GANONDORF.ino
 * Linear actuator releases a loop of string on command
 */

#include <Cutdown.h>

#define ACTUATOR_PIN 8   // actuator pin
#define XBEE_ADDR 04     // XBee channel

const long maxReleaseSeconds = 2000;    // Release timeout override
const long pulseSeconds = 60;         // Seconds for the actuator to run
char status;

Cutdown cutdown;

void setup()
{
    // initialize the trigger pin (digital) as output and set to low
    pinMode(ACTUATOR_PIN, OUTPUT);
    digitalWrite(ACTUATOR_PIN, LOW);

    // initialize cutdown library
    cutdown.begin();
}

void loop()
{
    cutdown.check_input();

    if (millis() > (maxReleaseSeconds * 1000))
    {
        cutdown.arm_system();
        cutdown.release = true;
    }

    if (cutdown.system_is_armed())
    {
        if (cutdown.release)
        {
            digitalWrite(ACTUATOR_PIN, HIGH);

            // disarm the system to prevent repeated firing
            cutdown.disarm_system();
            cutdown.send_release_confirmation();
            
            delay(pulseSeconds * 1000);
            digitalWrite(ACTUATOR_PIN, LOW);

        }
    }
}
