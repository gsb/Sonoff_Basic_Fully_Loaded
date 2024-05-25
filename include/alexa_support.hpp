//-----------------------------------------------------------------------------
#ifndef ALEXA_SUPPORT_HPP
#define ALEXA_SUPPORT_HPP

/*
FAUXMO ESP

Copyright (C) 2016-2020 by Xose Pérez <xose dot perez at gmail dot com>

The MIT License (MIT)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

//-----------------------------------------------------------------------------
//
// This code is a 'badly' hacked version of FAUXMO as credited above
//
// PURPOSE:
//   To enable Alexa-Voice-Commands for application control on an ESP
// 
// NOTES:
//   Each physical ESP [node] is assigned a custom IpAddress for easy maintenance
//   Every node is one 'logical' light type device for Alexa to control and monitor
//   The node's defaultDevice uses the ESP's default String name - espName
//   The espName defaults to "esp + WiFi.localIP()[3]" for ease of tracking
//   Alexa device monitoring is reflected on the defaultDevice's control page
//   Application set state and value changes to the defaultDevice are monitored by Alexa
//   Alexa changes for the node's defaultDevice are communicated as integer commands, 0-100
//   Alexa voice commands are implemented using Alexa app routines
//   Alexa app's defaultDevice control page, slider and on/off button, also initiate commands
//   Alexa app device page sliders do not force an 'ON' state change event when used


#define AVC4ESP_UDP_MULTICAST_IP         IPAddress(239,255,255,250)
#define AVC4ESP_UDP_MULTICAST_PORT       1900
#define AVC4ESP_TCP_MAX_CLIENTS          10
#define AVC4ESP_TCP_PORT                 1901
#define AVC4ESP_RX_TIMEOUT               3
#define AVC4ESP_DEVICE_UNIQUE_ID_LENGTH  12

///#define DEBUG_AVC4ESP  Serial
///#define DEBUG_AVC4ESP_VERBOSE_UDP  true
///#define DEBUG_AVC4ESP_VERBOSE_TCP  true

#ifdef DEBUG_AVC4ESP
  #define DEBUG_MSG_AVC4ESP(fmt, ...) { DEBUG_AVC4ESP.printf_P((PGM_P) PSTR(fmt), ## __VA_ARGS__); }
#else
  #define DEBUG_MSG_AVC4ESP(...)
#endif

#ifndef DEBUG_AVC4ESP_VERBOSE_TCP
  #define DEBUG_AVC4ESP_VERBOSE_TCP  false
#endif

#ifndef DEBUG_AVC4ESP_VERBOSE_UDP
  #define DEBUG_AVC4ESP_VERBOSE_UDP  false
#endif

#ifndef DEFAULT_INITIAL_PERCENT
  #define DEFAULT_INITIAL_PERCENT  50
#endif

#ifndef DEFAULT_INITIAL_STATE
  #define DEFAULT_INITIAL_STATE  true
#endif

#define LOCKOUT 500


//-----------------------------------------------------------------------------

#include <Arduino.h>
#include <functional>
#include <MD5Builder.h>
#include <queue>

#if defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
  #include <ESP8266HTTPClient.h>
#elif defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
  #include <esp_wifi.h>
  #include <AsyncTCP.h>
#else
  #error "Unsupported. Required: ARDUINO_ARCH_ESP32 OR ARDUINO_ARCH_ESP8266"
#endif//ARDUINO_ARCH_ESP32||ARDUINO_ARCH_ESP8266

#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>
#include <console_support.hpp>

extern std::queue<String>pending;
extern String espName;

class EventTask {
  private: 
    bool enabled;
    bool singleton;
    unsigned long lastTime;
    unsigned int period; // vs. unsigned long
    void (*callBackFunction)();

  public:
    EventTask() { };  // void constructor
    ~EventTask() { }; // void destructor
    void setup(unsigned long thePeriod, void (*theFunction)(), bool once=false) {
      setup(millis(), thePeriod, theFunction, once); //...call other set w/ four arguments
    };
  	void setup(unsigned long theLastTime, unsigned long thePeriod, void (*theFunction)(), bool once) {
      singleton = once;
      enabled = !singleton; // IFF singleton disable at setup
    	period = thePeriod;
    	lastTime = theLastTime;
    	callBackFunction = theFunction;	
    };
  	void update() {
      if (enabled && ((unsigned long)(millis()-lastTime) >= period)) {
        callBackFunction();
    	  lastTime = millis();
        singleton ? disable() : enable();
      }
    };
  	void disable() { enabled = false; lastTime = millis(); delay(1);}; // 9
  	void enable() { enabled = true; lastTime = millis(); delay(1);};   // 9

    /// NOTE: unused methods
  	///void reset() { lastTime = millis(); };
  	///void run() { if (enabled) callBackFunction(); };
  	///void setPeriod(unsigned long thePeriod) { period = thePeriod; };
    ///bool isEnabled() { return enabled; };
};


typedef struct {
    char * name;
    bool state;
    unsigned char value;
    unsigned char percent;
    char uniqueid[28];
      unsigned char _2value; // next value
      bool _2state;          // next state ... maybe
    EventTask defeater; //...to delay and/or defeat Alexa's forced state change
    void setState(bool _state) {
      state = _state; // set the ESP's default device state to 'on' or 'off'
	    //_2state = _state;   ///GSB
      //defeater.disable(); ///GSB
    };
} default_device_t;
#define device_t default_device_t*
default_device_t defaultDevice;


//------------------------------------------------------------------------------
//-- Alexa Support: Convert Alexa value (0-255) to percentage (1-100) and back
uint8_t pv_arr[] = {1,4,6,9,11,14,16,19,21,24,26,29,31,34,36,39,41,44,47,49,52,54,57,59,62,64,67,69,72,74,77,79,82,84,87,90,92,95,97,100,102,105,107,110,112,115,117,120,122,125,128,130,133,135,138,140,143,145,148,150,153,155,158,160,163,165,168,171,173,176,178,181,183,186,188,191,193,196,198,201,203,206,208,211,214,216,219,221,224,226,229,231,234,236,239,241,244,246,249,251,255};
uint8_t p2v(uint8_t p) {return (p>100)?255:pv_arr[p];} // 2-helper methods
uint8_t v2p(uint8_t v) {uint8_t p=0;for(;p<101&&v>pv_arr[p];p++);return(!p?0:(p<100?p:100));}

//typedef std::function<void(int, int)> TSetStateCallback;
typedef std::function<void(unsigned char)> TSetStateCallback;

class Avc4Esp {
  public:
    ~Avc4Esp();
    void _add(const char* device_name, uint8_t initP, bool initS);
		void createDevice(const char* device_name, uint8_t initPrecent, bool initState);
		void createDevice(const char* device_name, uint8_t initPrecent);
		void createDevice(const char* device_name);
		void createDevice(void);
    void setDeviceUniqueId(const char* uniqueid);
    void onSetState(TSetStateCallback fn) { _setCallback = fn; }
    bool process(AsyncClient *client, bool isGet, String url, String body);
    void setPort(unsigned long tcp_port) { _tcp_port = tcp_port; }
    void handle();
    void setupMulticast(void);

  private:
    AsyncServer * _server;
    WiFiUDP _udp;
    bool _enabled = true;
    bool _internal = false;
    unsigned int _tcp_port = AVC4ESP_TCP_PORT;
    AsyncClient * _tcpClients[AVC4ESP_TCP_MAX_CLIENTS];
    TSetStateCallback _setCallback = NULL;

    String _deviceJson(unsigned char id, bool all); // all = true means we are listing all devices so use full description template

    void _handleUDP();
    void _sendUDPResponse();
    void _onTCPClient(AsyncClient *client);
    bool _onTCPData(AsyncClient *client, void *data, size_t len);
    bool _onTCPRequest(AsyncClient *client, bool isGet, String url, String body);
    bool _onTCPDescription(AsyncClient *client, String url, String body);
    bool _onTCPList(AsyncClient *client, String url, String body);
    bool _onTCPControl(AsyncClient *client, String url, String body);
    void _sendTCPResponse(AsyncClient *client, const char * code, char * body, const char * mime);
    String _byte2hex(uint8_t zahl);
    String _makeMD5(String text);
};
Avc4Esp avc4esp; // instantiate a single global Alexa communications object


///-- End Avc4Esp.h
//-----------------------------------------------------------------------------

#ifndef HIDE_SHOW_TEMPLATES
PROGMEM const char AVC4ESP_TCP_HEADERS[] =
    "HTTP/1.1 %s\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %d\r\n"
    "Connection: close\r\n\r\n";

PROGMEM const char AVC4ESP_TCP_STATE_RESPONSE[] = "["
    "{\"success\":{\"/lights/1/state/on\":%s}},"
    "{\"success\":{\"/lights/1/state/bri\":%d}}"   // not needed?
"]";

// Working with gen1 and gen3, ON/OFF/%, gen3 requires TCP port 80
PROGMEM const char AVC4ESP_DEVICE_JSON_TEMPLATE[] = "{"
    "\"type\": \"Extended color light\","
    "\"name\": \"%s\","
    "\"uniqueid\": \"%s\","
    "\"modelid\": \"LCT015\","
    "\"manufacturername\": \"Philips\","
    "\"productname\": \"E4\","
    "\"state\":{"
        "\"on\": %s,"
	"\"bri\": %d,"
	"\"xy\": [0,0],"
	"\"hue\": 0,"
	"\"sat\": 0,"
	"\"effect\": \"none\","
	"\"colormode\": \"xy\","
	"\"ct\": 500,"
	"\"mode\": \"homeautomation\","
	"\"reachable\": true"
    "},"
    "\"capabilities\": {"
        "\"certified\": false,"
        "\"streaming\": {\"renderer\":true,\"proxy\":false}"
    "},"
    "\"swversion\": \"5.105.0.21169\""
"}";

// Use shorter description template when listing all devices
PROGMEM const char AVC4ESP_DEVICE_JSON_TEMPLATE_SHORT[] = "{"
    "\"type\": \"Extended color light\","
    "\"name\": \"%s\","
    "\"uniqueid\": \"%s\""

"}";

PROGMEM const char AVC4ESP_DESCRIPTION_TEMPLATE[] =
"<?xml version=\"1.0\" ?>"
"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
    "<specVersion><major>1</major><minor>0</minor></specVersion>"
    "<URLBase>http://%d.%d.%d.%d:%d/</URLBase>"
    "<device>"
        "<deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>"
        "<friendlyName>Philips hue (%d.%d.%d.%d:%d)</friendlyName>"
        "<manufacturer>Royal Philips Electronics</manufacturer>"
        "<manufacturerURL>http://www.philips.com</manufacturerURL>"
        "<modelDescription>Philips hue Personal Wireless Lighting</modelDescription>"
        "<modelName>Philips hue bridge 2012</modelName>"
        "<modelNumber>929000226503</modelNumber>"
        "<modelURL>http://www.meethue.com</modelURL>"
        "<serialNumber>%s</serialNumber>"
        "<UDN>uuid:2f402f80-da50-11e1-9b23-%s</UDN>"
        "<presentationURL>index.html</presentationURL>"
    "</device>"
"</root>";

PROGMEM const char AVC4ESP_UDP_RESPONSE_TEMPLATE[] =
    "HTTP/1.1 200 OK\r\n"
    "EXT:\r\n"
    "CACHE-CONTROL: max-age=100\r\n" // SSDP_INTERVAL
    "LOCATION: http://%d.%d.%d.%d:%d/description.xml\r\n"
    "SERVER: FreeRTOS/6.0.5, UPnP/1.0, IpBridge/1.17.0\r\n" // _modelName, _modelNumber
    "hue-bridgeid: %s\r\n"
    "ST: urn:schemas-upnp-org:device:basic:1\r\n"  // _deviceType
    "USN: uuid:2f402f80-da50-11e1-9b23-%s::upnp:rootdevice\r\n" // _uuid::_deviceType
    "\r\n";
#endif ///HIDE_SHOW_TEMPLATES
///-- End templates.h
//-----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// AVC UDP Section
// -----------------------------------------------------------------------------

void Avc4Esp::_sendUDPResponse() {

	DEBUG_MSG_AVC4ESP("[AVC4ESP] Responding to M-SEARCH request\n");

	IPAddress ip = WiFi.localIP();
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();

	char response[strlen(AVC4ESP_UDP_RESPONSE_TEMPLATE) + 128];
    snprintf_P(
      response, sizeof(response),
      AVC4ESP_UDP_RESPONSE_TEMPLATE,
      ip[0], ip[1], ip[2], ip[3],
	    _tcp_port,
      mac.c_str(), mac.c_str()
    );

	#if DEBUG_AVC4ESP_VERBOSE_UDP
    DEBUG_MSG_AVC4ESP("[AVC4ESP] UDP response sent to %s:%d\n%s", _udp.remoteIP().toString().c_str(), _udp.remotePort(), response);
	#endif

  _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
	#if defined(ESP32)
	  _udp.printf(response);
	#else
	  _udp.write(response);
	#endif
  _udp.endPacket();
}

void Avc4Esp::setupMulticast(void) {
#if defined(ARDUINO_ARCH_ESP32)
  _udp.beginMulticast(AVC4ESP_UDP_MULTICAST_IP, AVC4ESP_UDP_MULTICAST_PORT);
#elif defined(ARDUINO_ARCH_ESP8266)
  _udp.beginMulticast(WiFi.localIP(),AVC4ESP_UDP_MULTICAST_IP, AVC4ESP_UDP_MULTICAST_PORT);
#endif//ARDUINO_ARCH_ESP32||ARDUINO_ARCH_ESP8266
}


//-- Avc4Esp Setup
void avc4espSetup(const char* name) {

  // Many different ways to invoke Alexa for ESP control w/ name as "solehub":
  // "Alexa, turn solehub on" ("solehub" is the name of the default)
  // "Alexa, turn on solehub"
  // "Alexa, set solehub to fifty" (50 is bri-value of 128, 50% of brightness)
  // ...and most anything else via Alexa routines, i.e. "Alexa solehub on" or
  //    "Alexa solehub active" ... etc.

  avc4esp.setPort(80); // This is required for gen3 devices
	avc4esp.setupMulticast();

  // Create and add the default ESP's device object 'defaultDevice' with name,
  // value (1-254) and state true-active or false-inactive at ESP loop startup
  avc4esp.createDevice(); // create ESP's "defaultDevice" w/ name only or
  ///avc4esp.createDevice(name, 77, false); // w/ name, percent and state
}


// -----------------------------------------------------------------------------
// AVC TCP Section
// -----------------------------------------------------------------------------

void Avc4Esp::_sendTCPResponse(AsyncClient *client, const char * code, char * body, const char * mime) {
	char headers[strlen_P(AVC4ESP_TCP_HEADERS) + 32];
	snprintf_P(
		headers, sizeof(headers),
		AVC4ESP_TCP_HEADERS,
		code, mime, strlen(body)
	);

	#if DEBUG_AVC4ESP_VERBOSE_TCP
		DEBUG_MSG_AVC4ESP("[AVC4ESP] Response:\n%s%s\n", headers, body);
	#endif

	client->write(headers);
	client->write(body);
}

String Avc4Esp::_deviceJson(unsigned char id, bool all = true) {

	char buffer[strlen_P(AVC4ESP_DEVICE_JSON_TEMPLATE) + 64];

	if (all) {
		snprintf_P(
			buffer, sizeof(buffer),
			AVC4ESP_DEVICE_JSON_TEMPLATE,
			defaultDevice.name, defaultDevice.uniqueid,
			defaultDevice.state ? "true": "false",
			defaultDevice.value-1
		);
	} else {
		snprintf_P(
			buffer, sizeof(buffer),
			AVC4ESP_DEVICE_JSON_TEMPLATE_SHORT,
			defaultDevice.name, defaultDevice.uniqueid
		);
	}
	return String(buffer);
}

String Avc4Esp::_byte2hex(uint8_t zahl) {
  String hstring = String(zahl, HEX);
  if (zahl < 16) hstring = "0" + hstring;
  return hstring;
}

String Avc4Esp::_makeMD5(String text) {
  unsigned char bbuf[16];
  String hash = "";
  MD5Builder md5;
  md5.begin();
  md5.add(text);
  md5.calculate();
  md5.getBytes(bbuf);
  for (uint8_t i = 0; i < 16; i++) hash += _byte2hex(bbuf[i]);
  return hash;
}

bool Avc4Esp::_onTCPDescription(AsyncClient *client, String url, String body) {
	(void) url;
	(void) body;

	DEBUG_MSG_AVC4ESP("[AVC4ESP] Handling /description.xml request\n");
	IPAddress ip = WiFi.localIP();
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();

	char response[strlen_P(AVC4ESP_DESCRIPTION_TEMPLATE) + 64];
  snprintf_P(
    response, sizeof(response),
    AVC4ESP_DESCRIPTION_TEMPLATE,
    ip[0], ip[1], ip[2], ip[3], _tcp_port,
    ip[0], ip[1], ip[2], ip[3], _tcp_port,
    mac.c_str(), mac.c_str()
  );
	_sendTCPResponse(client, "200 OK", response, "text/xml");
	return true;
}

bool Avc4Esp::_onTCPList(AsyncClient *client, String url, String body) {
	int pos = url.indexOf("lights");
	if (-1 == pos) return false; // Not lights rerquest, so skip this
	unsigned char id = url.substring(pos+7).toInt(); // 0-ALL and 1-defaultDevice
	String response; // This will hold the response string	
	if (id == 0) { // Requesting information on all light-devices
		response += "{\"1\":" + _deviceJson(0, false) + "}";
	} else response = _deviceJson(0); // ...requesting information on just one
	_sendTCPResponse(client, "200 OK", (char *) response.c_str(), "application/json");
	return true;
}


static void set_state(void) { // eventer triggered timer callback method
  pending.push(String(defaultDevice._2state?"1":"0")); // queue for processing
}

void Avc4Esp::_handleUDP() { ///gsb

  defaultDevice.defeater.update(); // ...tick-off until triggered

	int len = _udp.parsePacket();
  if (len > 0) {
		unsigned char data[len+1];
    _udp.read(data, len);
    data[len] = 0;

		#if DEBUG_AVC4ESP_VERBOSE_UDP
			DEBUG_MSG_AVC4ESP("[AVC4ESP] UDP packet received\n%s", (const char *) data);
		#endif

    String request = (const char *) data;
    if (request.indexOf("M-SEARCH") >= 0) {
      if ((request.indexOf("ssdp:discover") > 0) || (request.indexOf("upnp:rootdevice") > 0) || (request.indexOf("device:basic:1") > 0)) {
        _sendUDPResponse();
      }
    }
  }
}

bool Avc4Esp::_onTCPControl(AsyncClient *client, String url, String body) {
	if (body.indexOf("devicetype") > 0) {
		_sendTCPResponse(client, "200 OK", (char *) "[{\"success\":{\"username\": \"2WLEDHardQrI3WHYTHoMcXHgEspsM8ZZRpSKtBQr\"}}]", "application/json");
		return true;
	}

	// Process and respond to a value or state request from Alexa  GSB
  if ((url.indexOf("state") > 0) && (body.length() > 0)) { // required
		int pos = url.indexOf("lights"); // required
		if (pos == -1) return false; //...not state-of-light request, skip it

    ///Serial.printf("**State request %lu, %lu\n", millis(), micros());
    ///Serial.printf("  URL: %s\n", url.c_str());
    ///Serial.printf(" BODY: %s\n", body.c_str());

    if((pos=body.indexOf("bri")) > 0) { // Find and get 'value' as an integer
      defaultDevice.defeater.disable();
      unsigned char val = body.substring(pos+5).toInt();
      pending.push(String(v2p(val)));
    } else {
      defaultDevice._2state = body.indexOf("false")>0 ? false : true;
      defaultDevice.defeater.enable();
    }

    // Finally, tell Alexa about the defaultDevice's current configuration
		char response[strlen_P(AVC4ESP_TCP_STATE_RESPONSE)+10];
		snprintf_P(response, sizeof(response), AVC4ESP_TCP_STATE_RESPONSE,
			  defaultDevice.state ? "true" : "false", defaultDevice.value>0 ? defaultDevice.value-1 : 0);
		_sendTCPResponse(client, "200 OK", response, "text/xml");

		return true;
	} // end of state request

	return false;
}


bool Avc4Esp::_onTCPRequest(AsyncClient *client, bool isGet, String url, String body) {
	#if DEBUG_AVC4ESP_VERBOSE_TCP
		DEBUG_MSG_AVC4ESP("isGet: %s\n", isGet ? "true" : "false");
		DEBUG_MSG_AVC4ESP("URL: %s\n", url.c_str());
		if (!isGet) DEBUG_MSG_AVC4ESP("Body:\n%s\n", body.c_str());
	#endif
	if (url.equals("/description.xml")) return _onTCPDescription(client, url, body);
	if (url.startsWith("/api")) {
		if (isGet) return _onTCPList(client, url, body);
		else return _onTCPControl(client, url, body);
	}
	return false;
}

bool Avc4Esp::_onTCPData(AsyncClient *client, void *data, size_t len) {
  if (!_enabled) return false;

	char * p = (char *) data;
	p[len] = 0;

	#if DEBUG_AVC4ESP_VERBOSE_TCP
		DEBUG_MSG_AVC4ESP("[AVC4ESP] TCP request\n%s\n", p);
	#endif

	char* method = p; // Method is the first word of the request
	while (*p != ' ') p++;
	*p = 0;
	p++;

	char * url = p; // Split word and flag start of url
	while (*p != ' ') p++; // Find next space
	*p = 0;
	p++;

	unsigned char c = 0; // Find double line feed
	while ((*p != 0) && (c < 2)) {
		if (*p != '\r') c = (*p == '\n') ? c + 1 : 0;
		p++;
	}
	char * body = p;
	bool isGet = (strncmp(method, "GET", 3) == 0);
	return _onTCPRequest(client, isGet, url, body);
}

void Avc4Esp::_onTCPClient(AsyncClient *client) {
  for (unsigned char i = 0; i < AVC4ESP_TCP_MAX_CLIENTS; i++) {
    if (!_tcpClients[i] || !_tcpClients[i]->connected()) {
      _tcpClients[i] = client;
      client->onAck([i](void *s, AsyncClient *c, size_t len, uint32_t time) { }, 0);
      client->onData([this, i](void *s, AsyncClient *c, void *data, size_t len) {
          _onTCPData(c, data, len);
        }, 0);

      client->onDisconnect([this, i](void *s, AsyncClient *c) {
          if(_tcpClients[i] != NULL) {
            _tcpClients[i]->free();
            _tcpClients[i] = NULL;
          } else {
            DEBUG_MSG_AVC4ESP("[AVC4ESP] Client %d already disconnected\n", i);
          }
          delete c;
          DEBUG_MSG_AVC4ESP("[AVC4ESP] Client #%d disconnected\n", i);
        }, 0);

      client->onError([i](void *s, AsyncClient *c, int8_t error) {
         DEBUG_MSG_AVC4ESP("[AVC4ESP] Error %s (%d) on client #%d\n", c->errorToString(error), error, i);
      }, 0);

      client->onTimeout([i](void *s, AsyncClient *c, uint32_t time) {
        DEBUG_MSG_AVC4ESP("[AVC4ESP] Timeout on client #%d at %i\n", i, time);
        c->close();
      }, 0);

      client->setRxTimeout(AVC4ESP_RX_TIMEOUT);

      DEBUG_MSG_AVC4ESP("[AVC4ESP] Client #%d connected\n", i);
      return;
    }
  }

  DEBUG_MSG_AVC4ESP("[AVC4ESP] Rejecting - Too many connections\n");

  client->onDisconnect([](void *s, AsyncClient *c) {
    c->free();
    delete c;
  });
  client->close(true);
}

// -----------------------------------------------------------------------------
// AVC Devices Section
// -----------------------------------------------------------------------------

Avc4Esp::~Avc4Esp() { /* ...nothing to do really */ }

void Avc4Esp::setDeviceUniqueId(const char *uniqueid) {
  strncpy(defaultDevice.uniqueid, uniqueid, AVC4ESP_DEVICE_UNIQUE_ID_LENGTH);
}

void Avc4Esp::_add(const char* device_name, uint8_t initPrecent, bool initState) {
  defaultDevice.name    = strdup(device_name); // init properties
	defaultDevice.state   = initState;
	defaultDevice._2state = initState;
  defaultDevice.percent = initPrecent;
  defaultDevice.value   = p2v(initPrecent);
	defaultDevice._2value = defaultDevice.value;
	defaultDevice.defeater.setup(LOCKOUT, set_state, true); // true singleton
  defaultDevice.defeater.disable(); ///GSB
  String mac_address    = WiFi.macAddress();
  snprintf(defaultDevice.uniqueid, 27, "%s:%s", mac_address.c_str(), "00:00-00"); // device_id = 0

  DEBUG_MSG_AVC4ESP("[AVC4ESP] Device '%s' added as %s w/ %d%% (%d)\n", 
      device_name, initState?"active":"inactive", initPrecent, defaultDevice.value);
  return;
}

void Avc4Esp::createDevice(const char* device_name, uint8_t initPrecent, bool initState) {
  if (initPrecent>100) initPrecent = 100;
  return _add(device_name, initPrecent, initState);
};

void Avc4Esp::createDevice(const char* device_name, uint8_t initPrecent) {
  if (initPrecent>100) initPrecent = 100;
  return _add(device_name, initPrecent, DEFAULT_INITIAL_STATE);
};

void Avc4Esp::createDevice(const char* device_name) {
  return _add(device_name, DEFAULT_INITIAL_PERCENT, DEFAULT_INITIAL_STATE);
};

void Avc4Esp::createDevice(void) {
  return _add(espName.c_str(), DEFAULT_INITIAL_PERCENT, DEFAULT_INITIAL_STATE);
};

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

// used by the server: server.onRequestBody(...) and server.onNotFound(...)
bool Avc4Esp::process(AsyncClient *client, bool isGet, String url, String body) {
	return _onTCPRequest(client, isGet, url, body);
}

// used for polling within Arduino loop
void Avc4Esp::handle() {
  if (_enabled) _handleUDP();
}

#endif//ALEXA_SUPPORT_HPP
//-----------------------------------------------------------------------------
