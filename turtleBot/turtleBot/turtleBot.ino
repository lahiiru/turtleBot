#include <FS.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <WebSocketsServer.h>
//#include <ESP8266WiFiMulti.h>
//#include <EEPROM.h>
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
#define HTTP_DOWNLOAD_UNIT_SIZE 1460
#define HTTP_UPLOAD_BUFLEN 2048
#define HTTP_MAX_DATA_WAIT 1000 //ms to wait for the client to send the request
#define HTTP_MAX_POST_WAIT 1000 //ms to wait for POST data to arrive
#define HTTP_MAX_CLOSE_WAIT 2000 //ms to wait for the client to close the connection
#define SPIFFS_ALIGNED_OBJECT_INDEX_TABLES 1

struct SvrSettings {
	SvrSettings() {
		ssid = "";
		password = "";
	}
	String ssid;
	String password;
};

unsigned long t_blink;
bool s_blink;
String inString = "";
byte parse_i = 0;
bool WS_MODE = true;
long t = 0;
int t_c = 0;
bool T_RX = true;
bool T_TX = false;

void blink(int d_blink) {
	if (millis() - t_blink > d_blink) {
		t_blink = millis();
		digitalWrite(pin_led, s_blink);
		s_blink = !s_blink;
	}
}

class AIOServer : public WebSocketsServer {
public:
	AIOServer(uint16_t port) :WebSocketsServer(port) {
		softAP_ssid = "TurtleBot";
		softAP_password = "12345678";
		apIP = IPAddress(192, 168, 4, 1);
		netMsk = IPAddress(255, 255, 255, 0);

	}
	void init() {
		DEBUG_SERIAL.print("Configuring access point....");
		/* You can remove the password parameter if you want the AP to be open. */
		WiFi.softAPConfig(apIP, apIP, netMsk);
		WiFi.softAP(softAP_ssid, softAP_password);
		delay(500); // Without delay I've seen the IP address blank
		DEBUG_SERIAL.print("AP IP address: ");
		DEBUG_SERIAL.println(WiFi.softAPIP());
			//EEPROM.begin(sizeof(config));
			/* Setup the DNS server redirecting all the domains to the apIP */
			//dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
			//dnsServer.start(DNS_PORT, "*", apIP);
		//updateParentConnection();

	}
	void wsSend(String str) {
		sendTXT(0, str);
	}
	void updateParentConnection() {
		DEBUG_SERIAL.println("Loading credentials...");
		loadCredentials(); // Load WLAN credentials from network
		connect = config.ssid.length() > 0; // Request WLAN connect if there is a SSID
	}
	void processRequest() {
		loop();
		//dnsServer.processNextRequest();
		if (!WS_MODE) {
			
			processParentConnection();
		}

	}
	virtual void handleNonWebsocketConnection(WSclient_t * client) override {
		if (WS_MODE && client->cUrl!="/wifi") {
			DEBUG_SERIAL.println("Rejecting client!");
			clientDisconnect(client);
			return;
		}
		cWebClient = client;
		String url = cWebClient->cUrl;
		DEBUG_SERIAL.println("New web client detected.");
		DEBUG_SERIAL.print("Requesting: "); DEBUG_SERIAL.println(url);
		/* Routing user to relevant page /*/
		if (url == "/") {
			handleRoot();
		}
		else if (url.startsWith("/remote")) {
			WS_MODE = true;
			handleRemote();
		}
		else if (url.startsWith("/wifisave")) {
			handleWifiSave();
		}
		else if (url.startsWith("/wifi")) {
			WS_MODE = false;
			handleWifi();
		}
		else if (url.startsWith("/generate_204")) {
			handleRoot();
		}
		else if (url.startsWith("/fwlink")) {
			handleRoot();
		}
		else {
			handleNotFound();
		}
		clientDisconnect(cWebClient);
	}
	void sendHeader(const String& name, const String& value) {
		sendHeader(name, value, false);
	}
	void sendHeader(const String& name, const String& value, bool first) {
		String headerLine = name;
		headerLine += ": ";
		headerLine += value;
		headerLine += "\r\n";

		if (first) {
			_responseHeaders = headerLine + _responseHeaders;
		}
		else {
			_responseHeaders += headerLine;
		}
	}
	void send(int code, const String& content_type, const String& content) {
		send(code, (const char*)content_type.c_str(), content);
	}
	void send(int code, const char* content_type, const String& content) {
		String header;
		_prepareHeader(header, code, content_type, content.length());
		sendContent(header);
		sendContent(content);
	}
private:
	String _responseHeaders;
	SvrSettings config;
	const char *softAP_ssid;
	const char *softAP_password;
	//const byte DNS_PORT = 53;
	//DNSServer dnsServer;
	IPAddress apIP;
	IPAddress netMsk;
	boolean connect;
	long lastConnectTry = 0;
	int status = WL_IDLE_STATUS;
	WSclient_t *cWebClient;
	String _hostHeader;

	void connectWifi() {
		Serial.println("Connecting as wifi client...");
		WiFi.disconnect();
		WiFi.begin(config.ssid.c_str(), config.password.c_str());
		int connRes = WiFi.waitForConnectResult();
		Serial.print("connRes: ");
		Serial.println(connRes);
	}

	void processParentConnection() {
		if (connect) {
			Serial.println("Connect requested");
			connect = false;
			connectWifi();
			lastConnectTry = millis();
		}
		{
			int s = WiFi.status();
			if (s == 0 && millis() > (lastConnectTry + 60000)) {
				/* If WLAN disconnected and idle try to connect */
				/* Don't set retry time too low as retry interfere the softAP operation */
				connect = true;
			}
			if (status != s) { // WLAN status change
				Serial.print("Status: ");
				Serial.println(s);
				status = s;
				if (s == WL_CONNECTED) {
					/* Just connected to WLAN */
					Serial.println("");
					Serial.print("Connected to ");
					Serial.println(config.ssid);
					Serial.print("IP address: ");
					Serial.println(WiFi.localIP());
					/*
					// Setup MDNS responder
					if (!MDNS.begin("remote.local")) {
						Serial.println("Error setting up MDNS responder!");
					}
					else {
						Serial.println("mDNS responder started");
						// Add service to MDNS-SD
						MDNS.addService("http", "tcp", 80);
					}
					*/
				}
				else if (s == WL_NO_SSID_AVAIL) {
					WiFi.disconnect();
				}
			}
		}
	}

	void sendContent(const String& content) {
		const size_t unit_size = HTTP_DOWNLOAD_UNIT_SIZE;
		size_t size_to_send = content.length();
		const char* send_start = content.c_str();

		while (size_to_send) {
			size_t will_send = (size_to_send < unit_size) ? size_to_send : unit_size;
			size_t sent = cWebClient->tcp->write(send_start, will_send);
			if (sent == 0) {
				break;
			}
			size_to_send -= sent;
			send_start += sent;
		}
	}

	void _prepareHeader(String& response, int code, const char* content_type, size_t contentLength) {
		response = "HTTP/1.1 ";
		response += String(code);
		response += " ";
		response += responseCodeToString(code);
		response += "\r\n";

		if (!content_type)
			content_type = "text/html";

		sendHeader("Content-Type", content_type, true);
		if (contentLength > 0)
			sendHeader("Content-Length", String(contentLength));
		sendHeader("Connection", "close");
		sendHeader("Access-Control-Allow-Origin", "*");

		response += _responseHeaders;
		response += "\r\n";
		_responseHeaders = String();
	}

	String responseCodeToString(int code) {
		switch (code) {
		case 100: return F("Continue");
		case 101: return F("Switching Protocols");
		case 200: return F("OK");
		case 201: return F("Created");
		case 202: return F("Accepted");
		case 203: return F("Non-Authoritative Information");
		case 204: return F("No Content");
		case 205: return F("Reset Content");
		case 206: return F("Partial Content");
		case 300: return F("Multiple Choices");
		case 301: return F("Moved Permanently");
		case 302: return F("Found");
		case 303: return F("See Other");
		case 304: return F("Not Modified");
		case 305: return F("Use Proxy");
		case 307: return F("Temporary Redirect");
		case 400: return F("Bad Request");
		case 401: return F("Unauthorized");
		case 402: return F("Payment Required");
		case 403: return F("Forbidden");
		case 404: return F("Not Found");
		case 405: return F("Method Not Allowed");
		case 406: return F("Not Acceptable");
		case 407: return F("Proxy Authentication Required");
		case 408: return F("Request Time-out");
		case 409: return F("Conflict");
		case 410: return F("Gone");
		case 411: return F("Length Required");
		case 412: return F("Precondition Failed");
		case 413: return F("Request Entity Too Large");
		case 414: return F("Request-URI Too Large");
		case 415: return F("Unsupported Media Type");
		case 416: return F("Requested range not satisfiable");
		case 417: return F("Expectation Failed");
		case 500: return F("Internal Server Error");
		case 501: return F("Not Implemented");
		case 502: return F("Bad Gateway");
		case 503: return F("Service Unavailable");
		case 504: return F("Gateway Time-out");
		case 505: return F("HTTP Version not supported");
		default:  return "";
		}
	}
	/** Handle root or redirect to captive portal */
	void handleRoot() {
		if (captivePortal()) { // If caprive portal redirect instead of displaying the page.
							   //return;
		}
		DEBUG_SERIAL.println("Serving root");
		sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		sendHeader("Pragma", "no-cache");
		sendHeader("Expires", "-1");
		send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
		sendContent(
			"<html><head></head><body>"
			"<h1>TurtleBot Server</h1>"
			"<p>This is the central point to access the turtleBot rover <br>which is capable of driving remotely via wifi access point.</p>"
			);
		if (cWebClient->tcp->localIP() == apIP) {
			sendContent(String("<p>You are connected through the soft AP: ") + softAP_ssid + "</p>");
			sendContent(String("<p>You may want to <a href='/wifi'>configure the server</a>.</p>"));
		}
		else {
			sendContent(String("<p>You are connected through the wifi network: ") + config.ssid + "</p>");
		}
		sendContent(
			"<p>You may want to <a href='/remote'>control the robot</a>.</p>"
			"</body></html>"
			);
		cWebClient->tcp->stop(); // Stop is needed because we sent no content length
	}

	String parseArgs(String arg) {
		String url = cWebClient->cUrl;
		if (url.indexOf("?") == -1)
			return "";
		String params = url.substring(url.indexOf("?") + 1, url.indexOf(" HTTP"));
		int i = params.indexOf(arg);
		if (i == -1)
			return "";
		params = params.substring(i + 1);
		i = params.indexOf("&");
		DEBUG_SERIAL.print("Parsed arg: ");
		if (i == -1) {
			DEBUG_SERIAL.println(params.substring(1));
			return params.substring(1);
		}
		else {
			DEBUG_SERIAL.println(params.substring(1, i));
			return params.substring(1, i);
		}
	}

	void parseHeaders() {
		String headerName;
		String headerValue;
		bool isForm = false;
		uint32_t contentLength = 0;
		//parse headers
		while (1) {
			String req = cWebClient->tcp->readStringUntil('\r');
			cWebClient->tcp->readStringUntil('\n');
			if (req == "")
				break;//no moar headers
			int headerDiv = req.indexOf(':');
			if (headerDiv == -1) {
				break;
			}
			headerName = req.substring(0, headerDiv);
			headerValue = req.substring(headerDiv + 1);
			headerValue.trim();
			DEBUG_SERIAL.print("headerName: ");
			DEBUG_SERIAL.println(headerName);
			DEBUG_SERIAL.print("headerValue: ");
			DEBUG_SERIAL.println(headerValue);
			if (headerName == "Host") {
				_hostHeader = headerValue;
			}
		}
	}

	void handleRemote() {
		DEBUG_SERIAL.println("Serving remote...");
		sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		sendHeader("Pragma", "no-cache");
		sendHeader("Expires", "-1");
		sendHeader("Server", "arduino-WebSocket-Server");
		send(200, "text/html", "");
		DEBUG_WEBSOCKETS("[WS-Server][%d][handleHeader] no Websocket connection close.\n", cWebClient->num);
		String path = "/index.htm";
		if (path.endsWith("/")) path += "index.htm";
		if (SPIFFS.exists(path)) {
			File file = SPIFFS.open(path, "r");
			String resp = file.readString();
			resp.replace("192.168.4.1", cWebClient->tcp->localIP().toString());
			sendContent(resp);
		}

		cWebClient->tcp->stop();
	}
	/** Redirect to captive portal if we got a request for another domain. Return true in that case so the page handler do not try to handle the request again. */
	boolean captivePortal() {
		parseHeaders();
		if (!isIp(_hostHeader) && _hostHeader != ("remote.local")) {
			DEBUG_SERIAL.print("Request redirected to captive portal");
			sendHeader("Location", String("http://") + toStringIp(cWebClient->tcp->localIP()), true);
			send(302, "text/plain", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
			cWebClient->tcp->stop(); // Stop is needed because we sent no content length
			return true;
		}
		return false;
	}
	/** Wifi config page handler */
	void handleWifi() {
		DEBUG_SERIAL.println("Serving wifi");
		sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		sendHeader("Pragma", "no-cache");
		sendHeader("Expires", "-1");
		send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
		sendContent(
			"<html><head></head><body>"
			"<h1>Wifi config</h1>"
			);
		if (cWebClient->tcp->localIP() == apIP) {
			sendContent(String("<p>You are connected through the soft AP: ") + softAP_ssid + "</p>");
		}
		else {
			sendContent(String("<p>You are connected through the wifi network: ") + config.ssid + "</p>");
			sendContent(String("<p><b>Access is denied!</b></p>"));
			cWebClient->tcp->stop();
			return;
		}
		sendContent(
			"\r\n<br />"
			"<table><tr><th align='left'>SoftAP config</th></tr>"
			);
		sendContent(String() + "<tr><td>SSID " + String(softAP_ssid) + "</td></tr>");
		sendContent(String() + "<tr><td>IP " + toStringIp(WiFi.softAPIP()) + "</td></tr>");
		sendContent(
			"</table>"
			"\r\n<br />"
			"<table><tr><th align='left'>WLAN config</th></tr>"
			);
		sendContent(String() + "<tr><td>SSID " + String(config.ssid) + "</td></tr>");
		sendContent(String() + "<tr><td>IP " + toStringIp(WiFi.localIP()) + "</td></tr>");
		sendContent(
			"</table>"
			"\r\n<br />"
			"<table><tr><th align='left'>WLAN list (refresh if any missing)</th></tr>"
			);
		DEBUG_SERIAL.println("scan start");
		int n = WiFi.scanNetworks();
		DEBUG_SERIAL.println("scan done");
		String inputSelect = "<select name='n'>";
		if (n > 0) {
			for (int i = 0; i < n; i++) {
				sendContent(String() + "\r\n<tr><td>" + WiFi.SSID(i) + String((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : " *") + " (" + (WiFi.RSSI(i) + 100) * 2 + "%)</td></tr>");
				inputSelect += String() + "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
			}
		}
		else {
			sendContent(String() + "<tr><td>No WLAN found</td></tr>");
		}
		inputSelect += "</select>";
		sendContent(
			String() +
			"</table>"
			"\r\n<br /><form method='GET' action='wifisave'><h4>Connect to network:</h4>"
			+ inputSelect +
			"<br /><input type='password' placeholder='password' name='p'/>"
			"<br /><input type='submit' value='Connect/Disconnect'/></form>"
			"<p>You may want to <a href='/'>return to the home page</a>.</p>"
			"</body></html>"
			);
		cWebClient->tcp->stop(); // Stop is needed because we sent no content length
		WiFi.scanDelete();
	}
	/** Load WLAN credentials from EEPROM */
	void loadCredentials() {
		config.ssid = "RoboServer";
		config.password = "124567890";
		//EEPROM.get(0, config);
		//EEPROM.end();
		DEBUG_SERIAL.println("Recovered credentials:");
		DEBUG_SERIAL.println(config.ssid);
		DEBUG_SERIAL.println(config.password.length() > 0 ? "********" : "<no password>");
	}
	/** Store WLAN credentials to EEPROM */
	void saveCredentials() {
		DEBUG_SERIAL.printf("Saving credentials, %s:%s\n", config.ssid.c_str(), config.password.c_str());
		/*
		EEPROM.put(0, config);
		EEPROM.commit();
		EEPROM.end();
		*/
	}
	/** Handle the WLAN save form and redirect to WLAN config page again */
	void handleWifiSave() {
		DEBUG_SERIAL.println("Serving wifi save");
		config.ssid = parseArgs("n");
		config.password = parseArgs("p");
		sendHeader("Location", "wifi", true);
		sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		sendHeader("Pragma", "no-cache");
		sendHeader("Expires", "-1");
		send(302, "text/plain", "");  // Empty content inhibits Content-length header so we have to close the socket ourselves.
		cWebClient->tcp->stop(); // Stop is needed because we sent no content length
		saveCredentials();
		connect = config.ssid.length() > 0; // Request WLAN connect with new credentials if there is a SSID
	}

	void handleNotFound() {
		if (captivePortal()) { // If caprive portal redirect instead of displaying the error page.
							   //return;
		}
		DEBUG_SERIAL.println("Serving not found");

		String message = "File Not Found\n\n";
		message += "URI: ";
		message += cWebClient->cUrl;
		sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		sendHeader("Pragma", "no-cache");
		sendHeader("Expires", "-1");
		send(404, "text/plain", message);
	}
	/** Is this an IP? */
	boolean isIp(String str) {
		for (int i = 0; i < str.length(); i++) {
			int c = str.charAt(i);
			if (c != '.' && (c < '0' || c > '9')) {
				return false;
			}
		}
		return true;
	}
	/** IP to String? */
	String toStringIp(IPAddress ip) {
		String res = "";
		for (int i = 0; i < 3; i++) {
			res += String((ip >> (8 * i)) & 0xFF) + ".";
		}
		res += String(((ip >> 8 * 3)) & 0xFF);
		return res;
	}
};

class Communicator {
private:
	//const char* host = "remote";
	//ESP8266WiFiMulti WiFiMulti;
	static AIOServer* webSocket;
public:
	IPAddress myIP;
	static String inBuffer;
	static String outBuffer;
	static void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {
		switch (type) {
		case WStype_DISCONNECTED:
			DEBUG_SERIAL.printf("[%u] Disconnected!\n", num);
			WS_MODE = false;
			break;
		case WStype_CONNECTED:
		{

			IPAddress ip = { 192,168,4,104 };//webSocket.remoteIP(num);
			DEBUG_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

			// send message to client
			Communicator::webSocket->sendTXT(num, "Connected");
			WS_MODE = true;
		}
		break;
		case WStype_TEXT:
			DEBUG_SERIAL.printf("[%u] get Text: %s\n", num, payload);
			Communicator::inBuffer = String((char *)payload);
			// send message to client
			// webSocket.sendTXT(num, "message here");
			// Communicator::webSocket->sendTXT(num, "Connected");
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

		//DEBUG_SERIAL.println();
		//DEBUG_SERIAL.println();
		//DEBUG_SERIAL.println();

		//for (uint8_t t = 4; t > 0; t--) {
		//	DEBUG_SERIAL.printf("[SETUP] BOOT WAIT %d...\n", t);
		//	DEBUG_SERIAL.flush();
		//	delay(1000);
		//}
		//this->WiFiMulti.addAP("RoboServer", "1234567890");

		//while (WiFiMulti.run() != WL_CONNECTED) {
		//	blink(100);
		//	delay(1);
		//}
		Communicator::webSocket = new AIOServer(80);
		Communicator::webSocket->begin();
		Communicator::webSocket->onEvent(webSocketEvent);

		DEBUG_SERIAL.println("Initialising AIOServer...");
		Communicator::webSocket->init();
		DEBUG_SERIAL.println("Communicator initialised.");
		return WL_CONNECTED;
	}
	void send(String str) {
		send(str, true);
	}

	void send(String str, bool override) {
		if (override) {
			Communicator::outBuffer = String(str);
		}
		else {
			Communicator::outBuffer += String(str);
		}

	}
	String receive() {
		String in = Communicator::inBuffer;
		Communicator::inBuffer = "";
		return in;
	}
	int toRead() {
		return Communicator::inBuffer.length();
	}
	void RX() {
		Communicator::webSocket->processRequest();
	}
	void TX() {
		if (Communicator::outBuffer.length() > 0) {
			Communicator::webSocket->wsSend(Communicator::outBuffer);
			//DEBUG_SERIAL.printf("Sending str: %s\n", Communicator::outBuffer.c_str());
			Communicator::outBuffer = "";
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
		this->speed_c = 0;
		this->drive();
	}
	void accelerate(int speed) {
		speed_c = speed;
		this->drive();
	}
	void steer(float radius) {
		this->z = radius; // COMPASS_FACTOR * 2;
		this->drive();
	}
	void printWheels() {
		DEBUG_SERIAL.println("Wheel speeds,");
		DEBUG_SERIAL.printf("{%i}={%i}. z=", left_c, right_c); DEBUG_SERIAL.println(z);
	}
	void sendRoverParams(Communicator* com) {
		com->send(String(left_c) + ":" + String(right_c));
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
			left_c = (_z < 1000) ? (int)(right_c * (_z - 0.5) / (_z + 0.5)) : speed_c;
		}
		else {  // turning right
			left_c = speed_c;
			right_c = (_z < 1000) ? (int)(left_c * (_z - 0.5) / (_z + 0.5)) : speed_c;
		}
		servol.write(7 + map(left_c, -1 * max_speed, max_speed, 0, 180));
		servor.write(180 + 5 - map(right_c, -1 * max_speed, max_speed, 0, 180));
	}
};

Rover *myRover;
Communicator *myCommunicator;
AIOServer *Communicator::webSocket;
String Communicator::inBuffer;
String Communicator::outBuffer;

void exec(float z, int s) {
	myRover->steer(z);
	myRover->accelerate(s);
	myRover->printWheels();
}

void scheduler() {
	if (millis() - t > 50) {
		if (t_c >= 4) {
			t_c = 0;
		}
		else {
			t_c += 1;
		}
		switch (t_c) {
		case 0:myCommunicator->RX(); break;
		case 1:myCommunicator->RX(); break;
		case 2:myCommunicator->RX(); break;
		case 3:myCommunicator->TX(); break;
		case 4:/* resting */break;
		}
		t = millis();
	}
}

void parseInBuffer() {
	if (myCommunicator->toRead()>0) {
		String in = myCommunicator->receive();
		//DEBUG_SERIAL.printf("Recieved str: %s\n", in.c_str());
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
		exec(m*x_sign, speed_c*y_sign);
		DEBUG_SERIAL.println("parsed.");
		DEBUG_SERIAL.println((int)(c[1] - 70) * 2);
		DEBUG_SERIAL.println((int)(c[2] - 70) * 2);
		DEBUG_SERIAL.println((int)((c[3] - 20)*360.0 / 100) - 90);
		DEBUG_SERIAL.println((int)c[4]);
	}
}

void processSerial() {
	if (DEBUG_SERIAL.available()) {
		int inChar = DEBUG_SERIAL.read();
		if (isDigit(inChar)) {
			// convert the incoming byte to a char
			// and add it to the string:
			inString += (char)inChar;
		}
		// if you get a newline, print the string,
		// then the string's value:
		if (inChar == '\n') {
			DEBUG_SERIAL.print("Value:");
			DEBUG_SERIAL.println(inString.toInt());
			exec(inString.toInt(), max_speed);
			myCommunicator->send(inString);
			// clear the string for new input:
			inString = "";
		}
	}
}

void setup() {
	pinMode(pin_led, OUTPUT);
	pinMode(pin_left_servo, OUTPUT);
	pinMode(pin_right_servo, OUTPUT);

	DEBUG_SERIAL.setDebugOutput(true);
	DEBUG_SERIAL.begin(115200);


	
	SPIFFS.begin();
	{
		Dir dir = SPIFFS.openDir("/");
	}

	DEBUG_SERIAL.println("FS started.");

	myCommunicator = new Communicator();
	DEBUG_SERIAL.println("Stariting communicator...");
	uint8_t s = myCommunicator->init();
	
	/*
	if (s != WL_CONNECTED) {
		DEBUG_SERIAL.println(s);
		DEBUG_SERIAL.println("Couldn't connect. Will restart after 1 min.");
		unsigned long t = millis();
		while (true)
		{myRover
			blink(100);             // quick blinks to indicate the error
			if (millis() - t > 600000) {
				ESP.restart();          // restart the CPU after 60 seconds
			}

		}
	}
	*/
	
	DEBUG_SERIAL.println("Communicater started.");

	myRover = new Rover();
	myRover->accelerate(0);
}
 
void loop() {
	blink(1000);
	//WiFi.disconnect();
	//DEBUG_SERIAL.println(WiFi.status());
	processSerial();
	scheduler();
	parseInBuffer();
	myRover->sendRoverParams(myCommunicator);
	delay(10);              // wait for a second
}