#include <FS.h>
#include <WebSocketsServer.h>
#include <ESP8266WiFiMulti.h>
#include <Servo.h>
#include <Math.h>

#define DEBUG true
#define pin_led D4
#define pin_left_servo D6
#define pin_right_servo D7
#define max_radius 1000
#define max_speed 500
#define MAX_SRV_CLIENTS 2
#define PORT 80
#define DEBUG_SERIAL Serial
#define COMPASS_FACTOR 45.0  //when z equals to this, speed of one wheel becomes zero (rotate about the tyre)

unsigned long t_blink;
bool s_blink;
String inString = "";
byte parse_i = 0;

void blink(int d_blink) {
	if (millis() - t_blink > d_blink) {
		t_blink = millis();
		digitalWrite(pin_led, s_blink);
		s_blink = !s_blink;
	}
}

class MyWSServer : public WebSocketsServer {
public:
	MyWSServer(uint16_t port) : WebSocketsServer(port) {}
	virtual void handleNonWebsocketConnection(WSclient_t * client) override {
		DEBUG_WEBSOCKETS("[WS-Server][%d][handleHeader] no Websocket connection close.\n", client->num);
		String resp = "HTTP/1.1 200 OK\r\nServer: arduino-WebSocket-Server\r\nContent-Type: text/html\r\nContent-Length: 10852\r\nKeep-Alive: timeout=5, max=100Connection: Keep-Alive\r\n\r\n";
		String path = "/index.htm";
		if (path.endsWith("/")) path += "index.htm";
		if (SPIFFS.exists(path)) {
			File file = SPIFFS.open(path, "r");
			resp += file.readString();
			this->sendContent(resp, client);
		}
		this->clientDisconnect(client);
	}
private:
	void sendContent(const String& content, WSclient_t * _currentClient) {
		const size_t unit_size = 1460;
		size_t size_to_send = content.length();
		const char* send_start = content.c_str();

		while (size_to_send) {
			size_t will_send = (size_to_send < unit_size) ? size_to_send : unit_size;
			size_t sent = _currentClient->tcp->write(send_start, will_send);
			if (sent == 0) {
				break;
			}
			size_to_send -= sent;
			send_start += sent;
		}
	}
};

class Communicator {
private:
	const char* MY_SSID = "turtleBot";
	const char* MY_PWD = "ttlbot123";
	const char* ssid = "RoboServer";
	const char* password = "124567890";
	const char* host = "remote";
	ESP8266WiFiMulti WiFiMulti;
	static MyWSServer* webSocket;
public:
	IPAddress myIP;
	static String inBuffer;
	static String outBuffer;
	static void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {
		switch (type) {
		case WStype_DISCONNECTED:
			DEBUG_SERIAL.printf("[%u] Disconnected!\n", num);
			break;
		case WStype_CONNECTED:
		{
			IPAddress ip = { 192,168,4,104 };//webSocket.remoteIP(num);
			DEBUG_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

			// send message to client
			Communicator::webSocket->sendTXT(num, "Connected");
		}
		break;
		case WStype_TEXT:
			DEBUG_SERIAL.printf("[%u] get Text: %s\n", num, payload);
			Communicator::inBuffer = String((char *)payload);
			// send message to client
			// webSocket.sendTXT(num, "message here");

			// send data to all connected clients
			// this->webSocket.broadcastTXT("message here");
			break;
		case WStype_BIN:
			DEBUG_SERIAL.printf("[%u] get binary lenght: %u\n", num, lenght);
			hexdump(payload, lenght);
			// send message to client
			// this->webSocket.sendBIN(num, payload, lenght);
			break;
		}
	}

	uint8_t init() {
		Communicator::inBuffer = "";
		Communicator::outBuffer = "";
		DEBUG_SERIAL.setDebugOutput(true);

		DEBUG_SERIAL.println();
		DEBUG_SERIAL.println();
		DEBUG_SERIAL.println();

		for (uint8_t t = 4; t > 0; t--) {
			DEBUG_SERIAL.printf("[SETUP] BOOT WAIT %d...\n", t);
			DEBUG_SERIAL.flush();
			delay(1000);
		}
		this->WiFiMulti.addAP("RoboServer", "1234567890");

		while (WiFiMulti.run() != WL_CONNECTED) {
			blink(100);
			delay(1);
		}
		Communicator::webSocket = new MyWSServer(80);
		Communicator::webSocket->begin();
		Communicator::webSocket->onEvent(webSocketEvent);

		return WL_CONNECTED;
	}
	void processCommunication() {
		if (Communicator::outBuffer.length()>0) {
			DEBUG_SERIAL.println("Pushing from buffer...");
			Communicator::webSocket->sendTXT(0, Communicator::outBuffer);
			Communicator::outBuffer = "";
		}
		Communicator::webSocket->loop();
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
		this->speed_c = 0;
		this->drive();
	}
	void accelerate(int speed) {
		speed_c = speed;
		this->drive();
	}
	void steer(int radius) {
		this->z = radius / COMPASS_FACTOR * 2;
		this->drive();
	}
	void printWheels() {
		Serial.println("Wheel speeds,");
		Serial.printf("{%i}={%i}.\n", left_c, right_c);
	}
private:
	Servo servol;
	Servo servor;
	int left_c;
	int right_c;
	int speed_c;
	float z = 0.0;
	void drive() {
		float _z = this->z;
		if (_z < 0) { // turning left
			_z = _z*-1;
			right_c = speed_c;
			left_c = (_z<max_radius / COMPASS_FACTOR * 2) ? (int)(right_c * (_z - 0.5) / (_z + 0.5)) : speed_c;
		}
		else {  // turning right
			left_c = speed_c;
			right_c = (_z<max_radius / COMPASS_FACTOR * 2) ? (int)(left_c * (_z - 0.5) / (_z + 0.5)) : speed_c;
		}
		servol.write(7 + map(left_c, -1 * max_speed, max_speed, 0, 180));
		servor.write(180 + 5 - map(right_c, -1 * max_speed, max_speed, 0, 180));
	}
};

Rover *myRover;
Communicator *myCommunicator;
MyWSServer *Communicator::webSocket;
String Communicator::inBuffer;
String Communicator::outBuffer;

void exec(int z, int s) {
	myRover->steer(z);
	myRover->accelerate(s);
	myRover->printWheels();
}

void parseInBuffer() {
	if (Communicator::inBuffer.length()>0) {
		String in = Communicator::inBuffer;
		Communicator::inBuffer = "";
		char* c = new char[6];
		in.toCharArray(c, 6);
		// x=r.cos(A), y=r.sin(A), r=50 (as javascript gives max r=100)
		int x = (c[1] - 70); // [-50,50]
		int y = (c[2] - 70);
		int y_sign = y == 0 ? 1 : y / abs(y);
		int x_sign = x == 0 ? 1 : x / abs(x);

		x = abs(x);
		y = abs(y);

		int speed_c = sqrt(x*x + y*y)*max_speed / 50.0;
		// y/x=tan(A)=m
		float m = 1.0f*y / (x + 0.01f);
		float z = 10 + (m - (tan(0.1))) / (1 + m*tan(0.1)) * 100;
		float A = atan(m)*57.29578;
		DEBUG_SERIAL.println("calculated values: speed,m,A,z");
		DEBUG_SERIAL.println(speed_c*y_sign);
		DEBUG_SERIAL.println(m);
		DEBUG_SERIAL.println(A);
		DEBUG_SERIAL.println((int)z*x_sign);
		DEBUG_SERIAL.println("wheel values,");
		exec((int)z*x_sign, speed_c*y_sign);
		DEBUG_SERIAL.println("parsed.");
		DEBUG_SERIAL.println((int)(c[1] - 70) * 2);
		DEBUG_SERIAL.println((int)(c[2] - 70) * 2);
		DEBUG_SERIAL.println((int)((c[3] - 20)*360.0 / 100) - 90);
		DEBUG_SERIAL.println((int)c[4]);
	}
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
			exec(inString.toInt(), max_speed);
			Communicator::outBuffer = inString;
			// clear the string for new input:
			inString = "";
		}
	}
}

void setup() {
	pinMode(pin_led, OUTPUT);
	pinMode(pin_left_servo, OUTPUT);
	pinMode(pin_right_servo, OUTPUT);
	Serial.begin(115200);
	SPIFFS.begin();
	{
		Dir dir = SPIFFS.openDir("/");
	}

	Serial.println("FS started.");

	myCommunicator = new Communicator();
	Serial.println("Stariting communicator...");
	uint8_t s = myCommunicator->init();
	if (s != WL_CONNECTED) {
		Serial.println(s);
		Serial.println("Couldn't connect. Will restart after 1 min.");
		unsigned long t = millis();
		while (true)
		{
			blink(100);             // quick blinks to indicate the error
			if (millis() - t > 600000) {
				ESP.restart();          // restart the CPU after 60 seconds
			}

		}
	}

	Serial.println("Communicater started.");

	myRover = new Rover();
	myRover->accelerate(0);
}

void loop() {
	blink(1000);
	processSerial();
	myCommunicator->processCommunication();
	parseInBuffer();
	delay(10);              // wait for a second
}