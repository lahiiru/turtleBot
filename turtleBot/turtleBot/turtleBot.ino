
#include <WiFiUdp.h>
#include <WiFiServer.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <ESP8266WiFiType.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFiGeneric.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266WiFi.h>
#include <Servo.h>


#define DEBUG true
#define pin_led D4
#define pin_left_servo D6
#define pin_right_servo D7
#define max_radius 500
#define MAX_SRV_CLIENTS 2
#define PORT 80

const char* MY_SSID = "turtleBot";
const char* MY_PWD = "ttlbot123";
const char* ssid = "NO FREE";
const char* password = "a92*eRrA9$";

WiFiServer server(PORT);
WiFiClient serverClients[MAX_SRV_CLIENTS];

class Communicator {
public:
	IPAddress myIP;

	void initAP() {
		WiFi.softAP(MY_SSID, MY_PWD);
		myIP = WiFi.softAPIP();
		Serial.print("AP IP address: ");
		Serial.println(myIP);
	}

	uint8_t init() {
		WiFi.begin(ssid, password);
		Serial.print("\nConnecting to "); Serial.println(ssid);
		uint8_t i = 0;
		while (WiFi.status() != WL_CONNECTED && i++ < 20) delay(500);    // if error occured retry to connect
		if (i == 21) {
			Serial.print("Could not connect to "); Serial.println(ssid);
			return server.status();
		}
		//start the server
		server.begin();
		server.setNoDelay(true);
		myIP = WiFi.localIP();

		Serial.print("Ready! Use 'telnet ");
		Serial.print(myIP.toString());
		Serial.print(" "); Serial.print(PORT); Serial.println("' to connect");

		return WL_CONNECTED;
	}

	void processCommunication() {
		this->acceptClients();
		this->RX();
		this->TX();
	}

private:
	void acceptClients() {
		//check if there are any new clients
		if (server.hasClient()) {
			for (uint8_t i = 0; i < MAX_SRV_CLIENTS; i++) {
				//find free/disconnected spot
				if (!serverClients[i] || !serverClients[i].connected()) {
					if (serverClients[i]) serverClients[i].stop();
					serverClients[i] = server.available();
					Serial.print("New client: "); Serial.print(i);
					continue;
				}
			}
			//no free/disconnected spot so reject
			WiFiClient serverClient = server.available();
			serverClient.stop();
		}
	}

	void RX() {
		//check clients for data
		for (uint8_t i = 0; i < MAX_SRV_CLIENTS; i++) {
			if (serverClients[i] && serverClients[i].connected()) {
				if (serverClients[i].available()) {
					//get data from the telnet client and push it to the UART
					while (serverClients[i].available()) Serial.write(serverClients[i].read());
				}
			}
		}
	}

	void TX() {
		//check UART for data
		if (Serial.available()) {
			size_t len = Serial.available();
			uint8_t sbuf[len];
			Serial.readBytes(sbuf, len);
			//push UART data to all connected telnet clients
			for (uint8_t i = 0; i < MAX_SRV_CLIENTS; i++) {
				if (serverClients[i] && serverClients[i].connected()) {
					serverClients[i].write(sbuf, len);
					delay(1);
				}
			}
		}
	}
};

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
bool s_blink;
String inString = "";
Rover *myRover;
Communicator *myCommunicator;

void setup() {
	pinMode(pin_led, OUTPUT);
	pinMode(pin_left_servo, OUTPUT);
	pinMode(pin_right_servo, OUTPUT);
	Serial.begin(115200);
	myCommunicator = new Communicator();
	Serial.println("Stariting communicator...");
	uint8_t s = myCommunicator->init();
	if (s != WL_CONNECTED) {
		Serial.println(s);
		Serial.println("Couldn't connect. Will restart after 1 min.");
		unsigned long t = millis();
		while (true)
		{
			blink(100);							// quick blinks to indicate the error
			if (millis() - t > 600000) {
				ESP.restart();					// restart the CPU after 60 seconds
			}

		}
	}
	Serial.println("Communicater started.");
	myRover = new Rover();
	myRover->accelerate(500);
}

void loop() {
	blink(1000);
	processSerial();
	myCommunicator->processCommunication();
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

void blink(int d_blink) {
	if (millis() - t_blink > d_blink) {
		t_blink = millis();
		digitalWrite(pin_led, s_blink);
		s_blink = !s_blink;
	}
}