// seconds after last keepalive when actuator will retract
int timeout_seconds = 600;

int ACTUATOR_PIN_HBRIDGE_A = 3;
int ACTUATOR_PIN_HBRIDGE_B = 4;

bool retracted = false;

void setup(void)
{
    pinMode(ACTUATOR_PIN_HBRIDGE_A, OUTPUT);
    pinMode(ACTUATOR_PIN_HBRIDGE_B, OUTPUT);

    digitalWrite(ACTUATOR_PIN_HBRIDGE_A, LOW);
    digitalWrite(ACTUATOR_PIN_HBRIDGE_B, LOW);

    retract(10);
    delay(6000);
    extend(6);
}

void loop(void)
{
    // if time on exceeds timeout seconds set in program constants, then retract actuator
    if (!retracted && millis() > timeout_seconds * 1000)
    {
        retract(10);
        retracted = true;
    }

    // wait a bit
    delay(1);
}

void extend(int pulse_seconds)
{
    controlActuator(1, pulse_seconds);
}

void retract(int pulse_seconds)
{
    controlActuator(-1, pulse_seconds);
}

void controlActuator(int direction, int pulse_seconds)
{
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
}
