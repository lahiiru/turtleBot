#include <Servo.h>

#define pin_led D4
#define pin_left_servo D6
#define pin_right_servo D7
#define max_radius 500

class Rover {
public:
	byte status;
	Rover() {
		this->status = 0;
		this->z = max_radius;
		this->accelerate(0);
		servol.attach(pin_left_servo);
		servor.attach(pin_right_servo);
	}
	void stop() {
		speed_c = 0;
		this->drive();
	}
	void accelerate(int speed) {
		speed_c = speed;
		this->drive();
	}
	void steer(int radius) {
		this->z = radius / 100.0;
		this->drive();
	}
private:
	Servo servol;
	Servo servor;
	int left_c;
	int right_c;
	int speed_c;
	float z = 0.0;
	void drive() {
		if (z < 0) { // turning left
			z = z*-1;
			right_c = speed_c;
			left_c = (z<max_radius) ? (int)(right_c * (z - 0.5) / (z + 0.5)) : speed_c;
		}
		else {  // turning right
			left_c = speed_c;
			right_c = (z<max_radius) ? (int)(left_c * (z - 0.5) / (z + 0.5)) : speed_c;
			Serial.println((int)(left_c * (z - 0.5) / (z + 0.5)));
		}
		servol.write(map(left_c, -500, 500, 0, 180));
		Serial.println(right_c);
		servor.write(180 - map(right_c, -500, 500, 0, 180));
	}

};

unsigned long t_blink;
int d_blink = 1000;
bool s_blink;
String inString = "";
Rover *myRover;
// the setup function runs once when you press reset or power the board
void setup() {
	// initialize digital pin 13 as an output.
	pinMode(pin_led, OUTPUT);
	pinMode(pin_left_servo, OUTPUT);
	pinMode(pin_right_servo, OUTPUT);
	Serial.begin(115200);
	myRover = new Rover();
	myRover->accelerate(500);
}

// the loop function runs over and over again forever
void loop() {
	blink();
	processSerial();
	delay(10);              // wait for a second
}

void processSerial() {
	if (Serial.available()) {
		int inChar = Serial.read();
		if (isDigit(inChar)) {
			// convert the incoming byte to a char
			// and add it to the string:
			inString += (char)inChar;
		}
		// if you get a newline, print the string,
		// then the string's value:
		if (inChar == '\n') {
			Serial.print("Value:");
			Serial.println(inString.toInt());
			exec(inString.toInt());
			// clear the string for new input:
			inString = "";
		}
	}
}

void exec(int i) {
	myRover->steer(i);
}

void blink() {
	if (millis() - t_blink > d_blink) {
		t_blink = millis();
		digitalWrite(pin_led, s_blink);
		s_blink = !s_blink;
	}
}