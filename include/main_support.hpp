//-----------------------------------------------------------------------------
#ifndef MAIN_SUPPORT_HPP
#define MAIN_SUPPORT_HPP

#define GPIO0   (0) //...physical button, except esp8266-01, where is relay pin.
#define GPIO1   (1) //...normally TX -- gsb: Blue LED ??
#define GPIO2   (2) // gsb: useable on Sonoff ??
#define GPIO3   (3) //...normally RX
#define GPIO12 (12) // Sonoff relay pin GPIO12; HIGH-on, LOW-off.
#define GPIO13 (13) // Sonoff green LED pin GPIO13; HIGH-off, LOW-on.

#define lamp LED_BUILTIN // use LED_BUILTIN as the POL lamp

#define DEFAULT_INITIAL_PERCENT  50
#define DEFAULT_INITIAL_STATE  true

//-----------------------------------------------------------------------------

#include <Arduino.h>
#include <credentials.hpp>
#include <functional>
#include <vector>
#include <queue>
#include <time.h>
#include <Updater.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266HTTPClient.h>
#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>
#include "LittleFS.h"
#include "FS.h"
#include <ArduinoOTA.h>
#include <console_support.hpp>
#include <alexa_support.hpp>
#include <EasyButton.h> // FYI: https://github.com/evert-arias/EasyButton/tree/main


//-- Globals, Prototypes and more ----------------------------------------------

uint8_t CustomIpAddress[]  = {192,168,2,BOARD_ID}; // static IP Address
uint8_t CustomMacAddress[] = {0,0,0,0,0,BOARD_ID}; // static MAC Address

String espName = String("esp")+BOARD_ID; // convention is espXXX
std::queue<String>pending;

void handlePendingQueue(void);
extern String getFilesList(void);
extern String ddStatus(void);
extern void displayBuildSummary(void);

static char incoming[MAX_MSG_SIZE+1];    // buffer to hold incoming packet
static char outgoing[MAX_MSG_SIZE+1];    // buffer to hold outgoing packet

AsyncWebServer server(HTTP_PORT);        // server port

WiFiUDP udpMsg;                          // Initialize UDP manager object
unsigned int udpMsgId = 0;               // UDP message sequencing number - ID
const char ACK[] = "@@";                 // initialize UDP acknowledgment str
unsigned int udpMsgPort = 7000 + HUB_ID; // shared UDP listening port


//-- Button Support -----------------------------------------------------------

EasyButton button(GPIO0); //  create instance of the button on GPIO0
EventTask clickEvent; //...to delay and/or defeat single click vs double click
static unsigned long buttonLastClicked = 0;


//-- TNP Time Support ---------------------------------------------------------

const char dayOfWeek[][16] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
const char* ntpServer = "time1.google.com"; ///"fritz.box";
const long  gmtOffset_sec = -18000; // -5 hours for SC/USA
const int   daylightOffset_sec = 3600; //...7200 ??
time_t now;                         // this is the epoch
tm espTimeInfo;                     // the structure tm holds time information in a more convient way
int Year;
int Month;
int Day;
int Hour;
int Minute;
int Second;
String AmPm;
int DayIndex;


//-- TP-Link Direct Plug Support via WiFi IP ----------------------------------
//  Credits and information here:  https://pastebin.com/PtXtKKPW  and...
//    https://www.reddit.com/r/tasker/comments/52lvhq/how_to_control_a_tplink_hs100
typedef struct { // Defined TP-Link Switch Structure
  String type;
  String name;
  String ip;
  String mac;
  bool state;
} switch_t;
static char packet_off[] = "\x00\x00\x00*\xd0\xf2\x81\xf8\x8b\xff\x9a\xf7\xd5\xef\x94\xb6\xc5\xa0\xd4\x8b\xf9\x9c\xf0\x91\xe8\xb7\xc4\xb0\xd1\xa5\xc0\xe2\xd8\xa3\x81\xf2\x86\xe7\x93\xf6\xd4\xee\xde\xa3\xde\xa3";
static char packet_on[]  = "\x00\x00\x00*\xd0\xf2\x81\xf8\x8b\xff\x9a\xf7\xd5\xef\x94\xb6\xc5\xa0\xd4\x8b\xf9\x9c\xf0\x91\xe8\xb7\xc4\xb0\xd1\xa5\xc0\xe2\xd8\xa3\x81\xf2\x86\xe7\x93\xf6\xd4\xee\xdf\xa2\xdf\xa2";
enum plugs { TESTPLUG, ANOTHERSWITCH }; // defines as needed...
switch_t Plugs[] = {
  {"HS103","TestPlug","192.168.2.117","48:22:54:D8:07:0F",true}, // Test Plug
  //...add other plugs as needed
};


//-----------------------------------------------------------------------------
//-- Common Support Methods ----------------------------------------------------

//-- update ESP application's global Date and Time variables
void updateEspTimeVars() {
  ///Year     = espTimeInfo.tm_year + 1900; // years since 1900
 Year     = espTimeInfo.tm_year%100;
 Month    = espTimeInfo.tm_mon + 1;     // January = 0 (!)
 Day      = espTimeInfo.tm_mday;
 Hour     = espTimeInfo.tm_hour>12?espTimeInfo.tm_hour%12:espTimeInfo.tm_hour;
 Minute   = espTimeInfo.tm_min;
 Second   = espTimeInfo.tm_sec; 
 AmPm     = espTimeInfo.tm_hour > 11 ? "PM" : "AM";
 DayIndex = espTimeInfo.tm_wday;
}

//-- get current Data and time as String
String getTimeStamp() {
  time(&now);                       // read the current time
  localtime_r(&now, &espTimeInfo);  // update the structure tm with the current time
  updateEspTimeVars();
	char buf[32];
  uint8_t len = snprintf(buf, sizeof(buf), "%02d/%02d/%02d %02d:%02d:%02d %s",
    Year, Month, Day, Hour, Minute, Second, AmPm.c_str());
  buf[len] = '\0';
  return buf;
}

//-- standard set TP_Link plug method
bool setPlugState(int index, bool mode) {
    AsyncClient* aClient = new AsyncClient();
    if (aClient->connect(Plugs[index].ip.c_str(), 9999)) {
      aClient->onConnect([&,aClient,index,mode] (void * arg, AsyncClient* client) {
        Plugs[index].state = mode; // record/save the current plug state
        if (mode) aClient->write((const char *)packet_on, 64);
        else aClient->write((const char *)packet_off, 64);
      });
      return true; // successful connection and new mode sent if needed
    }
  return false;  // failed connection, state unchanged
}

//-- Standard Physical Button Methods
static void doClick(void) { //...single click callback toggles Sonoff relay
  clickEvent.disable(); // double-click mechanics to bypass single click response
  if (digitalRead(GPIO12)!=HIGH) pending.push("1"); //...to ON
  else pending.push("0"); //...to OFF
}
static void doDoubleClick(void) { //...double click callback resets Plug w/ pause
  pending.push("3");
}
static void doHold(void) { //...hold callback resets both WIFI Routers and the Plug w/ pause
  pending.push("4");
}
void onClick(void) { // Callback function for simple button press and double press
  if (buttonLastClicked && millis()-buttonLastClicked < 750) { //...is double click
    buttonLastClicked = 0;
    clickEvent.disable();
    doDoubleClick();
  } else {
    buttonLastClicked = millis();
    clickEvent.enable(); // doClick() IFF triggered
  }
}
void onHold(void) { // Callback function button press and hold
  doHold(); 
}
void buttonSetup(void (*clicked)(void), void (*held)(void)) {
  button.begin(); // Initialize the button
  button.onPressed(clicked); // callback when the button is pressed
  button.onPressedFor(2000, held); // callback when the button is held down
	clickEvent.setup(750, doClick, true); // true is singleton event
}/// end: Standard Physical Button Methods


//-- Set ESP's static WiFi IPAddress
static void setCustomIpAddress() {
  IPAddress staticIP( {192, 168, 2, BOARD_ID} );
  IPAddress gateway(192, 168, 2, 1);
  IPAddress subnet(255, 255, 0, 0);
  IPAddress primaryDNS(8, 8, 8, 8);
  IPAddress secondaryDNS(8, 8, 4, 4);
  WiFi.config(staticIP, gateway, subnet, primaryDNS, secondaryDNS);
}

//-- Set a custom MAC address
void setCustomMacAddress() { //FYI: Greg's WiFi-router restricts IPAddress[3] to 100-199
  wifi_set_macaddr(STATION_IF, CustomMacAddress); // Set MAC address
}

//-- utility String to String-Tokens Vector, non-destructive
void str2tokens(String data, std::vector<String> &tokens, 
    size_t max=0, const char delim='/') { // defaults: to all tokens and SSV
  //...strip and tokenize the String data
  size_t i=0, k=data.length();
  while(i<=data.length() && data.charAt(i)==delim){ ++i; } // skip leading /'s
  while(--k>i && data.charAt(k)==delim); // ...and skip ending /'s
  for (size_t n=i; i <= k; ++i) {
    if (data.charAt(i) == delim || i == k) {
      tokens.push_back(data.substring(n,i==k?i+1:i));
      if (max && tokens.size() == max-1) {
        tokens.push_back(data.substring(i+1)); //...anything else as last
        return;
      }
      n = i+1;
    }
  }
  // FYI USAGE:
  // std::vector<String>tokens;     // Results - tokens list vector
  // str2tokens(msg, tokens,5,'/'); // 0-4 where tokens[4] is everything else
  // for(size_t i=0;i<tokens.size();i++)Serial.printf( ... );
  // UdpNet's Message SSV Format: CMND / DEST / ORIG / MSG_ID [ /data ]
  //     command - tokens[0]
  //  destintion - tokens[1]
  //      origin - tokens[2]
  //  message id - tokens[3]
  //  extra data - tokens[4] ...for command processing  (think another SSV list)
}//str2tokens


//-----------------------------------------------------------------------------
//-- UDP Messaging Support -----------------------------------------------------

//-- Handle Incomming UDP Messages
static void handleIncomingUdpMsg() {
  int packetSize = udpMsg.parsePacket();
  if (packetSize) { // IFF there is a packet available read it in now
    int len = udpMsg.read(incoming, MAX_MSG_SIZE);
    incoming[len] = '\0';
    String msg = incoming;

    ///Console.printf("%d :: inbound RAW [%s]\n", BOARD_ID, msg.c_str());

    // FYI:
    // just because NO_REPLY is set for this ESP does not stop other peers
    // from sending their ACK responses. Handle that case here as well 
    #ifndef NO_REPLY // client must send ACK reply per valid incoming message
      if (msg.indexOf("@@") == -1) { // msg is NOT a reply so send ACK back
        udpMsg.beginPacket(udpMsg.remoteIP(), udpMsg.remotePort());
        udpMsg.print(ACK);
        udpMsg.endPacket();
        udpMsg.flush();
        delay(1);
      } else { // is a another's ACK message, for now, just skip ACK processing
        ///Serial.printf("%d ++ [%d:%d %lu] :: %s\n", BOARD_ID, udpMsg.remoteIP()[3], udpMsg.remotePort(), millis(), msg.c_str() );
        return; // ...and exit any further incoming message processing
      }
    #endif//NO_REPLY

    // ...regardless, NO_REPLY or not, skip incoming ACKs fron others
    if (msg.indexOf("@@") == -1) { // is not an ACK reply from another node, so
      pending.push(msg); // queue as valid message for deferred processing ASAP
    }

  }// else, no packet available just now, so done

}//handleIncomingUdpMsg

//-- Core String directive processing
bool coreDirectives(String cmd) {

    if (cmd.equals("ver")) {
      Console.printf("%s\n", VERSION);
    }

    else if (cmd.equals("info")) {
      displayBuildSummary();
    }

    else if (cmd.equals("heap")) { // diaplay currently available heap
      Console.printf("%s available heap: %lu\n", espName.c_str(), ESP.getFreeHeap());
    }

    else if (cmd.equals("dev")) {
      Console.printf("%s\n", ddStatus().c_str());
    }

    else if (cmd.equals("relay")) { // toggle Sonoff relay
      digitalWrite(GPIO12, !digitalRead(GPIO12));
      Console.printf("Sonoff relay toggled to %s\n", digitalRead(GPIO12)?"ON":"OFF");
    }

    else if (cmd.equals("time")) { // display NTP local timestamp
      Console.printf("Current time is %s\n", getTimeStamp().c_str());
    }

    else if (cmd.equals("list")) { 
      Serial.println("\nFS listing:");
      String listing = getFilesList();
      Console.printf("%s \n", listing.c_str());
    }

    else return false; // no core directive found

    return true;
}

//-----------------------------------------------------------------------------
//-- Custom Setup Support Methods ---------------------------------------------

//-- Start USB Serial
void serialSetup(void) {
  Serial.begin(SERIAL_BAUD, SERIAL_8N1, SERIAL_TX_ONLY);
  Serial.setDebugOutput(false);
  Serial.println("\n");
}

//-- Connect to WLAN based on Credentials
void wifiSetup(void) {
  WiFi.disconnect(true); // JIC avoid a soft reboot hangup issue
    ///WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  setCustomIpAddress();  // required
  setCustomMacAddress(); // optional
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("\n\n  Connecting: ...");
  for (size_t tries=19; !WiFi.isConnected(); tries--) {
    if (!tries) {
      Serial.println("\nWiFi Failure ...rebooting again!\n");
      delay(2000);
      ESP.restart();
    }
    Serial.print('.');
    delay(500);
  }
  Serial.print("\n   Connected: ");Serial.println(WIFI_SSID);
}//wifiSetup

void udpnetSetup(void) {
  udpMsgId = 0;                 // reset message counter
  udpMsg.begin(udpMsgPort);     // EspUdpNet's core messaging mechanics
}

//-- Synchronise and manage ESP time
void ntpSetup() { /// via Google NTP server 'time1.google.com'
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  ///Serial.print("   Setup NTP: ..."); // visual awaiting statrup sync
  while (espTimeInfo.tm_year + 1900 < 2000 ) {
    time(&now);  // read the current time
    localtime_r(&now, &espTimeInfo);
    delay(100);
    Serial.print(".");
  } ///GSB perhaps add cycles limit and reboot now
  Serial.printf("\n         NTP: synchronsized\n");
}

//-- setup File System
void fsSetup(void) {
  if (LittleFS.begin()) Serial.println("    FS Mount: Successful");
  else Serial.println("  FS Mount: Failed (Try re-building and uploading file image.)");
}//serverSetup

//-- setup ESP's default Alexa device
void devSetup(void) {
  avc4espSetup(espName.c_str()); // defaultDevice's espName
}
String ddStatus(void) { // build a default device status string
	char status[64];
  uint8_t len = snprintf(status, sizeof(status), "%s/%s/%s/%d/%d",
      espName.c_str(), defaultDevice.name, (defaultDevice.state?"active":"inactive"),
      defaultDevice.value, v2p(defaultDevice.value));
  status[len] = '\0';
  return String(status);
};


//-- Setup OTA
void otaSetup() {
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // file system
      Serial.flush();
      type = "filesystem";
      LittleFS.end(); // unmounting FS
    }
    Console.printf("Start updating %s\n",type.c_str());
  });
  ArduinoOTA.onEnd([]() {
    Console.printf("\nEnd\n");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Console.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Console.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Console.printf("Auth Failed\n");
    else if (error == OTA_BEGIN_ERROR) Console.printf("Begin Failed\n");
    else if (error == OTA_CONNECT_ERROR) Console.printf("Connect Failed\n");
    else if (error == OTA_RECEIVE_ERROR) Console.printf("Receive Failed\n");
    else if (error == OTA_END_ERROR) Console.printf("End Failed\n");
  });
  ArduinoOTA.begin();
}


//-----------------------------------------------------------------------------
//-- LittleFS Files Management Support ----------------------------------------

void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  static File uploadFile; // File object to store the uploading file.
  if (!index) {
    if (!filename.startsWith("/")) filename = "/"+filename;
    uploadFile = LittleFS.open(filename, "w"); // Create persistent File object.
  }
  if ( uploadFile ) uploadFile.write(data, len);
  if ( final ) {
    if (uploadFile) {
      uploadFile.close();
      request->send(200, "text/plain", (filename+" uploaded!").c_str());
      Serial.printf("%s: File %s upload completed!\n", espName.c_str(), filename.c_str());
      if (filename.equals("cui.htm"))Console.shutdown(); // force reload of all CUIs
    } else {
      Serial.println("File upload failed.");
      request->send(500, "text/plain", "500: file create error.");
    }
  }
}

void handleDownload(AsyncWebServerRequest *request) {
  String target = request->url().substring(9); // /download/ and leave leading /
  while(target.endsWith("/"))target=target.substring(0,target.length()-1); // remove trailing /'s
  if ( target.length() > 0 ) {
    if (LittleFS.exists(target)) request->send(LittleFS, target.c_str(), "application/octet-stream");
    else request->send(404, "text/plain", "404: Not Found ");
  } else request->send( 400, "text/plain", "Download problem." );
  return; //...done.
}

void handleDelete(AsyncWebServerRequest *request) {
  String target = request->url().substring(7); // /deleate/ and leave leading /
  while ( target.endsWith("/") ) target = target.substring(0,target.length()-1);
  if ( target.length() > 0 ) {
    if ( LittleFS.exists(target) ) {
      LittleFS.remove(target);
      request->send( 200, "text/plain", target + " deleted." );
    } else request->send( 400, "text/plain", "Delete problem." );
  } else request->send( 400, "text/plain", "Delete problem." );
  return; //...done.
}

void handleClear(AsyncWebServerRequest *request) { // /clear/ and leave leading /
  String target = request->url().substring(6);
  while ( target.endsWith("/") ) target = target.substring(0,target.length()-1);
  if ( target.length() > 0 ) {
    if ( LittleFS.exists(target) ) {
      File file = LittleFS.open(target, "w");
      file.seek(0, SeekSet);
      file.close();
      request->send( 200, "text/plain", target + " cleared." );
    } else request->send( 400, "text/plain", "Clear problem." );
  } else request->send( 400, "text/plain", "Clear problem." );
  return; //...done.
}

void handleCreate(AsyncWebServerRequest *request) { // /create/ and leave leading /
  String target = request->url().substring(7);
  while ( target.endsWith("/") ) target = target.substring(0,target.length()-1);
  if ( target.length() > 0 ) {
    if ( !LittleFS.exists(target) ) {
      File file = LittleFS.open(target, "w");
      file.seek(0, SeekSet);
      file.close();
      request->send( 200, "text/plain", target + " created." );
    } else request->send( 400, "text/plain", "Create problem." );
  } else request->send( 400, "text/plain", "Create problem." );
  return; //...done.
}

String getContentType(String filename, bool download) { // get file MIME type
  if (download) {
    return "application/octet-stream";
  } else if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}

bool handleFileSend( AsyncWebServerRequest *request ) {
  String path = request->url();
  if (path.equals("/upload")) return true;
  if (path.endsWith("/")) path += "index.html";
  String gzPath = path + ".gz";
  if (LittleFS.exists(gzPath)) {
    Serial.println("File: " + path);
    request->send(LittleFS, gzPath.c_str(), (getContentType(gzPath,request->hasParam("download"))).c_str());
    return true;
  } else if (LittleFS.exists(path)) {
    Serial.println("File: " + path);
    request->send(LittleFS, path.c_str(), (getContentType(path,request->hasParam("download"))).c_str());
    return true;
  } else {
    Serial.println(path + "  : File Not Found!");
  }
  return false;
}

String getFilesList(void) { // get String list of available files
  String response = "";
  Dir dir = LittleFS.openDir("/");
  while (dir.next())response+=dir.fileName()+" / "+dir.fileSize()+"\r\n";
  return response;
}


//-----------------------------------------------------------------------------
//-- Setup HTTP WebServer
void serverSetup() {
  // Custom entry point (not required by the library, here just as an example)
  server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "turn device on!");
    pending.push("on");
  });

  server.on("/off", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "turn device off!");
    pending.push("off");
  });

  server.on("/ver", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", VERSION);
  });

  // Custom entry points, not required by the library, but here as examples
  server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
  	char response[192];
    uint8_t len = snprintf(response, sizeof(response),
        " VERSION: %s\nESP Name: %s\n WiFi IP: %s\n     MAC: %s",
        VERSION, WiFi.localIP().toString().c_str(), espName.c_str(), WiFi.macAddress().c_str());
    response[len] = '\0';
    Serial.println(response);
    request->send(200, "text/text", response);
  });

  server.on("/dev", HTTP_GET, [](AsyncWebServerRequest *request) {
    String response = ddStatus();
    Serial.println(response.c_str());
    request->send(200, "text/text", response.c_str());
  });

  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("\nFS listing:");
    String listing = getFilesList();
    Serial.print(listing);
    request->send( 200, "text/plain", listing );
  });

  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/upload.html", "text/html" ); // LittleFS
    ///request->send_P(200, "text/html", upload_html);  // internal
  });
  server.onFileUpload(handleFileUpload); //...processing

  // Next two callbacks are required for Alexa gen1/gen3 devices compatibility
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (avc4esp.process(request->client(), request->method() == HTTP_GET, request->url(), String((char *)data))) return;
    // Handle any other body request here...
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    String body = (request->hasParam("body", true)) ? request->getParam("body", true)->value() : String();
    if (avc4esp.process(request->client(), request->method() == HTTP_GET, request->url(), body)) return;

    if (request->url().startsWith("/api")) { //...skip other api calls
      request->send(200, "text/plain", "OK");
      return;
    }

    //-- Server 'on starts with' extensions ----
    if ( request->url().startsWith("/download/") ) {
      return handleDownload(request);
    }
    if ( request->url().startsWith("/delete/") ) {
      return handleDelete(request);
    }
    if ( request->url().startsWith("/clear/") ) {
      return handleClear(request);
    }
    if ( request->url().startsWith("/create/") ) {
      return handleCreate(request);
    }

    // ... add more if (path.startsWith("/...")) {...} ...as needed

    String msg = request->url().substring(1); // as integer command request?
    if (msg.toInt() > 0 || (char)'0' == msg.charAt(0)) {
      String response("Set device to ");
      request->send(200, "text/plain", (response+=msg));
      pending.push(msg);
      return;
    }

    // Finally, assume it might be a simple LittleFS file transfer request
    if (handleFileSend(request)) return; // LittleFS file send

    Console.printf("NOT_FOUND: %s  %lu", msg.c_str(), millis());
  });

  Console.begin(&server); // Start ConsoleUI using current server instance, and

  server.begin();

}//serverSetup


//-----------------------------------------------------------------------------
//-- Print to IDE USB Monitor Build Summary
void displayBuildSummary(void) {
  Console.printf("    ESP Name: %s\n", espName.c_str());
  Console.printf("     Version: %s\n", VERSION);
	Console.printf("  Build Time: %s at %s\n", __DATE__, __TIME__);
  Console.printf("  IP address: %s\n", WiFi.localIP().toString().c_str());
  Console.printf(" MAC address: %s\n", WiFi.macAddress().c_str());
  delay(100);
  if (LittleFS.exists("/favicon.ico")) Console.printf(" File System: Mounted\n");
  else Console.printf(" File System: Failed - rebuild and upload\n");
  Console.printf("  UDP Server: %d\n", udpMsgPort);
  Console.printf("Current time: %s\n", getTimeStamp().c_str());
  Console.printf("Current heap: %lu\n", ESP.getFreeHeap());
  Console.printf(" Default Dev: %s\n", ddStatus().c_str());
  Console.printf("       Relay: %s\n", digitalRead(GPIO13)==HIGH?"OFF":"ON");
}//displayBuildSummary


#endif//MAIN_SUPPORT_HPP
//-----------------------------------------------------------------------------