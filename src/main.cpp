//-----------------------------------------------------------------------------

#include <Arduino.h>
#include "main_support.hpp"

/* Sonoff_Basic_Fully_Loaded  ...or maybe overloaded!

  Includes:
    - Web monitor page w/ user control - WebSocket connection
    - Alexa Voice Control
    - WLAN UDP messaging within ESP network
    - LittleFS file system w/ file: upload, download, create, clear and delete
    - Multi-functional Button; tap, double-tap and hold
    - Local Data and Time maintenance: i.e. 24/05/25 02:56:44 PM
    - TP-Link Smart plug on/off direct WiFi control
    - HTTP rest request server support (API)
    - Arduino code update via OTA

  External resource use:
    - browser for the Web monitor,
    - AWS for Alexa Voice Control, and
    - Google's NTP server for date and time management

*/


//-- Arduino core setup method ------------------------------------------------

void setup()
{
  pinMode(GPIO13, OUTPUT);     // lamp mode for "proof of life" blinker - POL
  pinMode(GPIO0, INPUT);       // button pin - HIGH-off, LOW-on
  pinMode(GPIO12, OUTPUT);     // relay pin GPIO12 - HIGH-on, LOW-off
  digitalWrite(GPIO12, HIGH);  // initially, set relay as on
  digitalWrite(GPIO13, LOW);   // initially, set lamp as on

  serialSetup();                // alowing repurposing Serial's RX and TX pins
  wifiSetup();                  // w/ custom MAC and IP Addresses
  serverSetup();                // full HTTP and WebSockets functionality
  ntpSetup();                   // startup NTP access
  otaSetup();                   // setup Arduino's OTA for text and data
  udpnetSetup();                // setup EspUdpNet 
  fsSetup();                    // mount LittleFS 
  Console.shutdown();           // ensure CUIs restart upon reboot
  devSetup();                   // ESP's default alexa device setup i.e. defaultDevice
  buttonSetup(onClick, onHold); // setup button callbacks

  // Example uses TP-Link Plug, initialized here
  setPlugState(TESTPLUG, (Plugs[TESTPLUG].state)); // default initialization
}


//-- Arduino core loop method -------------------------------------------------
void loop()
{
  //-- Async HeartBeats as a one-second 'heartbeat' timer loop (optional)
  static unsigned long heartbeats = 0;
  static unsigned long lastUpdated = 0;
  unsigned long currentMillis = millis();
  for (unsigned int delta=0; // loop correction factor
    (delta=(currentMillis-lastUpdated)) >= 1000; // roll-over error possible
    lastUpdated=(currentMillis-(delta%1000)),
    ++heartbeats)
  {
    if (heartbeats) { // do every 1-sec loop except very first i.e. loop zero

      // Proof-of-Life (POL) blinker - toggle GPIO13 IFF relay GPIO12 is OFF
      if (digitalRead(GPIO12)==LOW) digitalWrite(GPIO13, !digitalRead(GPIO13));

      if (!(heartbeats%20)) // send test message to 'HUB_ID' via EspUdpNet
      {
        int n = sprintf(outgoing, "/test/%d/%d/%u/%u/%lu", HUB_ID, BOARD_ID,
            udpMsgId++, ESP.getFreeHeap(), millis()); // DEMO
        outgoing[n] = '\0';
        udpMsg.beginPacket({192,168,2,HUB_ID}, udpMsgPort);
        udpMsg.print(outgoing);
        udpMsg.endPacket();
        udpMsg.flush();
        Console.printf("%d >> %s \n", BOARD_ID, outgoing); // optional
      }

    }
    else {  // first loop, i.e. heartbeats is zero, so finish setup now

      while(pending.size()) pending.pop(); // cleanup before polling starts

      displayBuildSummary(); // displays a short build summary
    }

  }///HeartBeats Loop

  if (heartbeats) { // IFF not zero, check all polling maintenance methods
    handlePendingQueue();   // process first in FIFO pending queue and if any
    avc4esp.handle();       // poll and process Alexa's UDP packet requests
    handleIncomingUdpMsg(); // handle incoming udpMsg packets from other ESPs
    ArduinoOTA.handle();    // process OTA for code updates
    button.read();          // button event polling for tap and hold events
    clickEvent.update();    // single click lockout check until triggered or cancelled
  }

}//loop


//-- Message Processing Directives --------------------------------------------
void handlePendingQueue()
{
  if (pending.size()) {           // IFF a message is queued, process 1st only
    String msg = pending.front(); // ...so, get first queued message, remove it 
    pending.pop();                // from queue and begin processing it below

    std::vector<String>tokens;       // list of String-tokens via spliting message
    str2tokens(msg, tokens, 5, '/'); // capture 5 tokenized fields in SSV string
      // Project's Message SSV Format: CMND / DEST / ORIG / MSG_ID [ /data ]
      //     command - tokens[0]
      //  destintion - tokens[1]
      //      origin - tokens[2]
      //  message id - tokens[3]
      //  extra data - tokens[4] data for command processing, if any needed
    
    if (!tokens.size()) return; // if message is not properly formatted...

    // FYI:
    //  Integer CMDs
    //   0 - Sonoff's relay off
    //   1 - Sonoff's relay on
    //   2 - reboot Sonoff
    //   3 - toggle testPlug
    //   4 - reset testPlug w/ delay
    //   others, 5-100 as from Alexa

    ///...optional: convert these String messages to common String integers
    Console.printf("%d << %s\n", BOARD_ID, msg.c_str());
    if (msg.equals("off")) msg = "0";
    else if (msg.equals("on")) msg = "1";
    else if (msg.equals("reboot")) msg = "2";
    else if (msg.equals("togglePlug")) msg = "3";
    else if (msg.equals("recyclePlug")) msg = "4";

    uint8_t percent = msg.toInt();

    if (percent || (percent==0 && (char)'0'== msg.charAt(0)))
    { // Process msg as an integer command

      if (percent>100)percent=100; // force boundries as 0-100
      unsigned char value = p2v(percent);

      switch (percent) { // Process 'integer' command (2-100) - from any source

        case 0:
          defaultDevice.state = false;
          if (digitalRead(GPIO12)==HIGH) { // skip IFF not currently 'on'
            digitalWrite(GPIO12, LOW); // turn relay 'off'
            digitalWrite(GPIO13, HIGH);// Sonoff LED pin GPIO13; HIGH-off, LOW-on
            Console.printf("%s: relay switched OFF!\n", espName.c_str());
          }
          Console.printf("%s\n", ddStatus().c_str());
          break;

        case 1:
          defaultDevice.state = true;
          if (digitalRead(GPIO12)==LOW) { // skip IFF not currently 'off'
            digitalWrite(GPIO12, HIGH); // turn relay 'on'
            digitalWrite(GPIO13, LOW);// Sonoff green LED pin GPIO13; HIGH-off, LOW-on
            Console.printf("%s: relay switched ON!\n", espName.c_str());
          }
          Console.printf("%s\n", ddStatus().c_str());
          break;

        case 2:
          Console.printf("...rebooting Sonoff Switch!\n");
          delay(1000);
          ESP.restart();
          break; //...superfluous, unreachable

        case 3:  // toggle TestPlug
          Console.printf("...toggle test plug  %lu\n", millis());
          setPlugState(TESTPLUG, !Plugs[TESTPLUG].state);  // turn TESTPLUG off
          break;

        case 4:  // recycle TestPlug w/ wait
          Console.printf("...recycle test plug  %lu  ...waiting\n", millis());
          setPlugState(TESTPLUG, false); // turn TESTPLUG off
          for (int n=0; n<RECYCLEDELAY*4; n++) { //...visual for reseting
            digitalWrite(GPIO13, !digitalRead(GPIO13));
            delay(250);
          }
          setPlugState(TESTPLUG, true);   // turn TESTPLUG on
          Console.printf("Test plug should be back on again  %lu\n", millis());
          break;

        default: // ... default for all other percentages
          defaultDevice.value = value;
          defaultDevice.percent = percent;
          Console.printf("%s to %d: %s\n", espName.c_str(), percent, ddStatus().c_str());
          break;
      }

    } // was int command

    // ELSE: was not int command so check if a String directive is defined

    else if (tokens[0].equals("test")) { // test UDP messaging
      // ... test message has no real processing
    }

    // ... add more user directives here as needed

    else if (!coreDirectives(tokens[0])) { // check for core commands as well
     Console.printf("No Processing Directive for '%s'\n", msg.c_str());
    }

  }///if message pending...

}//handlePendingQueue

//-----------------------------------------------------------------------------