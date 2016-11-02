#define ACTUATOR_PIN 2

void setup() 
{
  pinMode(ACTUATOR_PIN, OUTPUT);
}

void loop() 
{
  // retract
  fire(1, 10);
  delay(5000);
  // extend
  fire(2, 10);
  delay(5000);
}

void fire(int direction, int pulse_seconds)
{  
    unsigned long start_milliseconds = millis();
    while (millis() <= start_milliseconds + (pulse_seconds * 1000))
    {
        digitalWrite(ACTUATOR_PIN, HIGH);
        delay(direction);
        digitalWrite(ACTUATOR_PIN, LOW);
        delay(direction);
    }

    delay(500);
}
