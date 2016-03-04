#EM Cutdown Module
##Developed for the Space Systems Laboratory at the University of Maryland

Board: Arduino Uno connected to BMP180 pressure sensor

##Pins
electromagnet is on pin 13 (Arduino Uno)
BMP180 green wire -> SDA
BMP180 blue wire -> SCL
BMP180 white wire -> 3.3v
BMP180 black wire -> GND

##Installation
Copy this folder into your_arduino_workspace/ and use the Arduino IDE (https://www.arduino.cc/en/Main/Software) to upload

##Dependencies
- https://github.com/UMDBPP/Balloonduino.git
- https://github.com/UMDBPP/BMP180.git
