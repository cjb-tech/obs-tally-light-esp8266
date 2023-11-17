#include "OBS_tally_light.hpp"

#ifndef VERSION
#define VERSION "dev"
#endif

// #define DEBUG_LED_STRIP
#define FASTLED_ALLOW_INTERRUPTS 0

#ifndef CHIP_FAMILY
#define CHIP_FAMILY "Unknown"
#endif

#ifndef VERSION
#define VERSION "Unknown"
#endif

#define DISPLAY_NAME "Tally Light"

//Include libraries:
#if ESP32
#include <esp_wifi.h>
#include <WebServer.h>
#include <WiFi.h>
#else
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#endif

#include <EEPROM.h>
#include "./OBSWebsocket4.cpp"
#include <FastLED.h>

#if ESP32
//Define LED1 color pins
#ifndef PIN_RED1
#define PIN_RED1 32
#endif
#ifndef PIN_GREEN1
#define PIN_GREEN1 33
#endif
#ifndef PIN_BLUE1
#define PIN_BLUE1 25
#endif

//Define LED2 color pins
#ifndef PIN_RED2
#define PIN_RED2 26
#endif
#ifndef PIN_GREEN2
#define PIN_GREEN2 27
#endif
#ifndef PIN_BLUE2
#define PIN_BLUE2 14
#endif

#else  // ESP8266
//Define LED1 color pins
#ifndef PIN_RED1
#define PIN_RED1 D0
#endif
#ifndef PIN_GREEN1
#define PIN_GREEN1 D2
#endif
#ifndef PIN_BLUE1
#define PIN_BLUE1 D1
#endif

//Define LED2 color pins
#ifndef PIN_RED2
#define PIN_RED2 D4
#endif
#ifndef PIN_GREEN2
#define PIN_GREEN2 D5
#endif
#ifndef PIN_BLUE2
#define PIN_BLUE2 D6
#endif
#endif

//Define LED colors
#define LED_OFF 0
#define LED_RED 1
#define LED_GREEN 2
#define LED_BLUE 3
#define LED_YELLOW 4
#define LED_PINK 5
#define LED_WHITE 6
#define LED_ORANGE 7

//Map "old" LED colors to CRGB colors
CRGB color_led[8] = { CRGB::Black, CRGB::Red, CRGB::Lime, CRGB::Blue, CRGB::Yellow, CRGB::Fuchsia, CRGB::White, CRGB::Orange };

//Define states
#define STATE_STARTING 0
#define STATE_CONNECTING_TO_WIFI 1
#define STATE_CONNECTING_TO_OBS 2
#define STATE_RUNNING 3

//Define modes of operation
#define MODE_NORMAL 1
#define MODE_PREVIEW_STAY_ON 2
#define MODE_PROGRAM_ONLY 3
#define MODE_ON_AIR 4

#define TALLY_FLAG_OFF 0
#define TALLY_FLAG_PROGRAM 1
#define TALLY_FLAG_PREVIEW 2

//Define Neopixel status-LED options
#define NEOPIXEL_STATUS_FIRST 1
#define NEOPIXEL_STATUS_LAST 2
#define NEOPIXEL_STATUS_NONE 3

//FastLED
#ifndef TALLY_DATA_PIN
#if ESP32
#define TALLY_DATA_PIN 12
#elif ARDUINO_ESP8266_NODEMCU
#define TALLY_DATA_PIN 7
#else
#define TALLY_DATA_PIN 13  // D7
#endif
#endif
int numTallyLEDs;
int numStatusLEDs;
CRGB *leds;
CRGB *tallyLEDs;
CRGB *statusLED;
bool neopixelsUpdated = false;

//Initialize global variables
#if ESP32
WebServer server(80);
#else
ESP8266WebServer server(80);
#endif

OBSSocket4 OBSSocket;

uint8_t state = STATE_STARTING;

//Define struct for holding tally settings (mostly to simplify EEPROM read and write, in order to persist settings)
struct Settings {
  char tallyName[32] = "";
  char sourceName[32] = "";
  uint8_t tallyModeLED1;
  uint8_t tallyModeLED2;
  bool staticIP;
  IPAddress tallyIP;
  IPAddress tallySubnetMask;
  IPAddress tallyGateway;
  IPAddress obsIP;
  uint16_t neopixelsAmount;
  uint8_t neopixelStatusLEDOption;
  uint8_t neopixelBrightness;
  uint8_t ledBrightness;
};

Settings settings;

bool firstRun = true;

int bytesAvailable = false;
uint8_t readByte;

//Perform initial setup on power on
void setup() {
  //Init pins for LED
  pinMode(PIN_RED1, OUTPUT);
  pinMode(PIN_GREEN1, OUTPUT);
  pinMode(PIN_BLUE1, OUTPUT);

  pinMode(PIN_RED2, OUTPUT);
  pinMode(PIN_GREEN2, OUTPUT);
  pinMode(PIN_BLUE2, OUTPUT);

  setBothLEDs(LED_BLUE);

  //Start Serial
  Serial.begin(74880);
  Serial.println("########################");
  Serial.println("Serial started");

  //Read settings from EEPROM. WIFI settings are stored separately by the ESP
  EEPROM.begin(sizeof(settings));  //Needed on ESP8266 module, as EEPROM lib works a bit differently than on a regular Arduino
  EEPROM.get(0, settings);

  //Initialize LED strip
  if (0 < settings.neopixelsAmount && settings.neopixelsAmount <= 1000) {
    leds = new CRGB[settings.neopixelsAmount];
    FastLED.addLeds<NEOPIXEL, TALLY_DATA_PIN>(leds, settings.neopixelsAmount);

    if (settings.neopixelStatusLEDOption != NEOPIXEL_STATUS_NONE) {
      numStatusLEDs = 1;
      numTallyLEDs = settings.neopixelsAmount - numStatusLEDs;
      if (settings.neopixelStatusLEDOption == NEOPIXEL_STATUS_FIRST) {
        statusLED = leds;
        tallyLEDs = leds + numStatusLEDs;
      } else {  // if last or or other value
        statusLED = leds + numTallyLEDs;
        tallyLEDs = leds;
      }
    } else {
      numTallyLEDs = settings.neopixelsAmount;
      numStatusLEDs = 0;
      tallyLEDs = leds;
    }
  } else {
    settings.neopixelsAmount = 0;
    numTallyLEDs = 0;
    numStatusLEDs = 0;
  }

  FastLED.setBrightness(settings.neopixelBrightness);
  setSTRIP(LED_OFF);
  setStatusLED(LED_BLUE);
  FastLED.show();

  Serial.println(settings.tallyName);

  if (settings.staticIP && settings.tallyIP != IPADDR_NONE) {
    WiFi.config(settings.tallyIP, settings.tallyGateway, settings.tallySubnetMask);
  } else {
    settings.staticIP = false;
  }

  //Put WiFi into station mode and make it connect to saved network
  WiFi.mode(WIFI_STA);
#if ESP32
  WiFi.setHostname(settings.tallyName);
#else
  WiFi.hostname(settings.tallyName);
#endif
  WiFi.setAutoReconnect(true);
  WiFi.begin();

  Serial.println("------------------------");
  Serial.println("Connecting to WiFi...");
  Serial.println("Network name (SSID): " + getSSID());

  // Initialize and begin HTTP server for handeling the web interface
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.onNotFound(handleNotFound);
  server.begin();

  //Set state to connecting before entering loop
  changeState(STATE_CONNECTING_TO_WIFI);
}

void loop() {
  switch (state) {
    case STATE_CONNECTING_TO_WIFI:
      if (WiFi.status() == WL_CONNECTED) {
        WiFi.mode(WIFI_STA);  // Disable softAP if connection is successful
        Serial.println("------------------------");
        Serial.println("Connected to WiFi:   " + getSSID());
        Serial.println("IP:                  " + WiFi.localIP().toString());
        Serial.println("Subnet Mask:         " + WiFi.subnetMask().toString());
        Serial.println("Gateway IP:          " + WiFi.gatewayIP().toString());
        changeState(STATE_CONNECTING_TO_OBS);
      } else if (firstRun) {
        firstRun = false;
        Serial.println("Unable to connect. Serving \"Tally Light setup\" WiFi for configuration, while still trying to connect...");
        WiFi.softAP((String)DISPLAY_NAME + " setup");
        WiFi.mode(WIFI_AP_STA);  // Enable softAP to access web interface in case of no WiFi
        setBothLEDs(LED_WHITE);
        setStatusLED(LED_WHITE);
      }
      break;

    case STATE_CONNECTING_TO_OBS:
      // Initialize a connection to the OBS:
      if (firstRun) {
        OBSSocket.begin(settings.obsIP);
        OBSSocket.connect();
        Serial.println("------------------------");
        Serial.println("Connecting to OBS...");
        Serial.println((String) "OBS IP:         " + settings.obsIP[0] + "." + settings.obsIP[1] + "." + settings.obsIP[2] + "." + settings.obsIP[3]);
        firstRun = false;
      }
      OBSSocket.runLoop();
      if (OBSSocket.isConnected()) {
        changeState(STATE_RUNNING);
        Serial.println("Connected to OBS");
      }
      break;

    case STATE_RUNNING:
      //Handle data exchange and connection to swithcher
      OBSSocket.runLoop();

      //Switch state if ATEM connection is lost...
      if (!OBSSocket.isConnected()) {  // will return false if the connection was lost
        Serial.println("------------------------");
        Serial.println("Connection to OBS lost...");
        changeState(STATE_CONNECTING_TO_OBS);
      } else {
        //Set LED and Neopixel colors accordingly
        int color = getLedColor(settings.tallyModeLED1, String(settings.sourceName));
        setLED1(color);
        setSTRIP(color);

        color = getLedColor(settings.tallyModeLED2, String(settings.sourceName));
        setLED2(color);
      }
      break;
  }

  //Switch state if WiFi connection is lost...
  if (WiFi.status() != WL_CONNECTED && state != STATE_CONNECTING_TO_WIFI) {
    Serial.println("------------------------");
    Serial.println("WiFi connection lost...");
    changeState(STATE_CONNECTING_TO_WIFI);

    //Force atem library to reset connection, in order for status to read correctly on website.
    OBSSocket.begin(settings.obsIP);
    OBSSocket.connect();
  }

  //Show strip only on updates
  if (neopixelsUpdated) {
    FastLED.show();
    neopixelsUpdated = false;
  }

  //Handle web interface
  server.handleClient();
}

//Handle the change of states in the program
void changeState(uint8_t stateToChangeTo) {
  firstRun = true;
  switch (stateToChangeTo) {
    case STATE_CONNECTING_TO_WIFI:
      state = STATE_CONNECTING_TO_WIFI;
      setBothLEDs(LED_BLUE);
      setStatusLED(LED_BLUE);
      setSTRIP(LED_OFF);
      break;
    case STATE_CONNECTING_TO_OBS:
      state = STATE_CONNECTING_TO_OBS;
      setBothLEDs(LED_PINK);
      setStatusLED(LED_PINK);
      setSTRIP(LED_OFF);
      break;
    case STATE_RUNNING:
      state = STATE_RUNNING;
      setBothLEDs(LED_GREEN);
      setStatusLED(LED_ORANGE);
      break;
  }
}

//Set the color of both LEDs
void setBothLEDs(uint8_t color) {
  setLED(color, PIN_RED1, PIN_GREEN1, PIN_BLUE1);
  setLED(color, PIN_RED2, PIN_GREEN2, PIN_BLUE2);
}

//Set the color of the 1st LED
void setLED1(uint8_t color) {
  setLED(color, PIN_RED1, PIN_GREEN1, PIN_BLUE1);
}

//Set the color of the 2nd LED
void setLED2(uint8_t color) {
  setLED(color, PIN_RED2, PIN_GREEN2, PIN_BLUE2);
}

//Set the color of a LED using the given pins
void setLED(uint8_t color, int pinRed, int pinGreen, int pinBlue) {
  digitalWrite(pinRed, 0);
  digitalWrite(pinGreen, 0);
  digitalWrite(pinBlue, 0);
#if ESP32
  switch (color) {
    case LED_OFF:
      digitalWrite(pinRed, 0);
      digitalWrite(pinGreen, 0);
      digitalWrite(pinBlue, 0);
      break;
    case LED_RED:
      digitalWrite(pinRed, 1);
      digitalWrite(pinGreen, 0);
      digitalWrite(pinBlue, 0);
      break;
    case LED_GREEN:
      digitalWrite(pinRed, 0);
      digitalWrite(pinGreen, 1);
      digitalWrite(pinBlue, 0);
      break;
    case LED_BLUE:
      digitalWrite(pinRed, 0);
      digitalWrite(pinGreen, 0);
      digitalWrite(pinBlue, 1);
      break;
    case LED_YELLOW:
      digitalWrite(pinRed, 1);
      digitalWrite(pinGreen, 1);
      digitalWrite(pinBlue, 0);
      break;
    case LED_PINK:
      digitalWrite(pinRed, 1);
      digitalWrite(pinGreen, 0);
      digitalWrite(pinBlue, 1);
      break;
    case LED_WHITE:
      digitalWrite(pinRed, 1);
      digitalWrite(pinGreen, 1);
      digitalWrite(pinBlue, 1);
      break;
  }
#else
  uint8_t ledBrightness = settings.ledBrightness;
  void (*writeFunc)(uint8_t, uint8_t);
  if (ledBrightness >= 0xff) {
    writeFunc = &digitalWrite;
    ledBrightness = 1;
  } else {
    writeFunc = &analogWriteWrapper;
  }

  switch (color) {
    case LED_OFF:
      digitalWrite(pinRed, 0);
      digitalWrite(pinGreen, 0);
      digitalWrite(pinBlue, 0);
      break;
    case LED_RED:
      writeFunc(pinRed, ledBrightness);
      digitalWrite(pinGreen, 0);
      digitalWrite(pinBlue, 0);
      break;
    case LED_GREEN:
      digitalWrite(pinRed, 0);
      writeFunc(pinGreen, ledBrightness);
      digitalWrite(pinBlue, 0);
      break;
    case LED_BLUE:
      digitalWrite(pinRed, 0);
      digitalWrite(pinGreen, 0);
      writeFunc(pinBlue, ledBrightness);
      break;
    case LED_YELLOW:
      writeFunc(pinRed, ledBrightness);
      writeFunc(pinGreen, ledBrightness);
      digitalWrite(pinBlue, 0);
      break;
    case LED_PINK:
      writeFunc(pinRed, ledBrightness);
      digitalWrite(pinGreen, 0);
      writeFunc(pinBlue, ledBrightness);
      break;
    case LED_WHITE:
      writeFunc(pinRed, ledBrightness);
      writeFunc(pinGreen, ledBrightness);
      writeFunc(pinBlue, ledBrightness);
      break;
  }
#endif
}

void analogWriteWrapper(uint8_t pin, uint8_t value) {
  analogWrite(pin, value);
}

//Set the color of the LED strip, except for the status LED
void setSTRIP(uint8_t color) {
  if (numTallyLEDs > 0 && tallyLEDs[0] != color_led[color]) {
    for (int i = 0; i < numTallyLEDs; i++) {
      tallyLEDs[i] = color_led[color];
    }
    neopixelsUpdated = true;
#ifdef DEBUG_LED_STRIP
    Serial.println("Tally:  ");
    printLeds();
#endif
  }
}

//Set the single status LED (last LED)
void setStatusLED(uint8_t color) {
  if (numStatusLEDs > 0 && statusLED[0] != color_led[color]) {
    for (int i = 0; i < numStatusLEDs; i++) {
      statusLED[i] = color_led[color];
      if (color == LED_ORANGE) {
        statusLED[i].fadeToBlackBy(230);
      } else {
        statusLED[i].fadeToBlackBy(0);
      }
    }
    neopixelsUpdated = true;
#ifdef DEBUG_LED_STRIP
    Serial.println("Status: ");
    printLeds();
#endif
  }
}

#ifdef DEBUG_LED_STRIP
void printLeds() {
  for (int i = 0; i < settings.neopixelsAmount; i++) {
    Serial.print(i);
    Serial.print(", RGB: ");
    Serial.print(leds[i].r);
    Serial.print(", ");
    Serial.print(leds[i].g);
    Serial.print(", ");
    Serial.println(leds[i].b);
  }
  Serial.println();
}
#endif

int getTallyState(String tallyName) {
  if (OBSSocket.getProgramScene().equals(tallyName)) {
    return TALLY_FLAG_PROGRAM;
  }
  if (OBSSocket.getPreviewScene().equals(tallyName)) {
    return TALLY_FLAG_PREVIEW;
  }
  return TALLY_FLAG_OFF;
}

int getLedColor(int tallyMode, String tallyName) {
  if (tallyMode == MODE_ON_AIR) {
    if (OBSSocket.getStreamStreaming()) {
      return LED_RED;
    }
    return LED_OFF;
  }

  int tallyState = getTallyState(tallyName);

  if (tallyState == TALLY_FLAG_PROGRAM) {  //if tally live
    return LED_RED;
  } else if ((tallyState == TALLY_FLAG_PREVIEW       //if tally preview
              || tallyMode == MODE_PREVIEW_STAY_ON)  //or preview stay on
             && tallyMode != MODE_PROGRAM_ONLY) {    //and not program only
    return LED_GREEN;
  } else {  //if tally is neither
    return LED_OFF;
  }
}

//Serve setup web page to client, by sending HTML with the correct variables
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset=\"ASCII\"><meta name=\"viewport\"content=\"width=device-width,initial-scale=1.0\"><title>Tally Light setup</title></head><script>function switchIpField(e){console.log(\"switch\");console.log(e);var target=e.srcElement||e.target;var maxLength=parseInt(target.attributes[\"maxlength\"].value,10);var myLength=target.value.length;if(myLength>=maxLength){var next=target.nextElementSibling;if(next!=null){if(next.className.includes(\"IP\")){next.focus();}}}else if(myLength==0){var previous=target.previousElementSibling;if(previous!=null){if(previous.className.includes(\"IP\")){previous.focus();}}}}function ipFieldFocus(e){console.log(\"focus\");console.log(e);var target=e.srcElement||e.target;target.select();}function load(){var containers=document.getElementsByClassName(\"IP\");for(var i=0;i<containers.length;i++){var container=containers[i];container.oninput=switchIpField;container.onfocus=ipFieldFocus;}containers=document.getElementsByClassName(\"tIP\");for(var i=0;i<containers.length;i++){var container=containers[i];container.oninput=switchIpField;container.onfocus=ipFieldFocus;}toggleStaticIPFields();}function toggleStaticIPFields(){var enabled=document.getElementById(\"staticIP\").checked;document.getElementById(\"staticIPHidden\").disabled=enabled;var staticIpFields=document.getElementsByClassName('tIP');for(var i=0;i<staticIpFields.length;i++){staticIpFields[i].disabled=!enabled;}}</script><style>a{color:#0F79E0}</style><body style=\"font-family:Verdana;white-space:nowrap;\"onload=\"load()\"><table cellpadding=\"2\"style=\"width:100%\"><tr bgcolor=\"#142839\"style=\"color:#ffffff;font-size:.8em;\"><td colspan=\"3\"><h1>&nbsp;" + (String)DISPLAY_NAME + " setup</h1><h2>&nbsp;Status:</h2></td></tr><tr><td><br></td><td></td><td style=\"width:100%;\"></td></tr><tr><td>Connection Status:</td><td colspan=\"2\">";
  switch (WiFi.status()) {
    case WL_CONNECTED:
      html += "Connected to network";
      break;
    case WL_NO_SSID_AVAIL:
      html += "Network not found";
      break;
    case WL_CONNECT_FAILED:
      html += "Invalid password";
      break;
    case WL_IDLE_STATUS:
      html += "Changing state...";
      break;
    case WL_DISCONNECTED:
      html += "Station mode disabled";
      break;
#if ESP32
    default:
#else
    case -1:
#endif
      html += "Timeout";
      break;
  }

  html += "</td></tr><tr><td>Network name (SSID):</td><td colspan=\"2\">";
  html += getSSID();
  html += "</td></tr><tr><td><br></td></tr><tr><td>Signal strength:</td><td colspan=\"2\">";
  html += WiFi.RSSI();
  html += " dBm</td></tr>";
  //Commented out for users without batteries
  // html += "<tr><td><br></td></tr><tr><td>Battery voltage:</td><td colspan=\"2\">";
  // html += dtostrf(uBatt, 0, 3, buffer);
  // html += " V</td></tr>";
  html += "<tr><td>Static IP:</td><td colspan=\"2\">";
  html += settings.staticIP == true ? "True" : "False";
  html += "</td></tr><tr><td>" + (String)DISPLAY_NAME + " IP:</td><td colspan=\"2\">";
  html += WiFi.localIP().toString();
  html += "</td></tr><tr><td>Subnet mask: </td><td colspan=\"2\">";
  html += WiFi.subnetMask().toString();
  html += "</td></tr><tr><td>Gateway: </td><td colspan=\"2\">";
  html += WiFi.gatewayIP().toString();
  html += "</td></tr><tr><td><br></td></tr>";
  html += "<tr><td>OBS status:</td><td colspan=\"2\">";
  if (OBSSocket.isRejected())
    html += "<span style=\"color: #bb0000; background-color: #ffaaaa;\">Connection rejected - No empty spot</span>";
  else if (OBSSocket.isConnected())
    html += "<span style=\"color: #00bb00; background-color: #aaffaa;\">Connected</span>";  // - Wating for initialization";
  else if (WiFi.status() == WL_CONNECTED)
    html += "<span style=\"color: #ffffff; background-color: #ff55ff;\">Disconnected - No response from OBS</span>";
  else
    html += "<span style=\"color: #ffffff; background-color: #2121ff;\">Disconnected - Waiting for WiFi</span>";
  html += "</td></tr><tr><td>OBS IP:</td><td colspan=\"2\">";
  html += (String)settings.obsIP[0] + '.' + settings.obsIP[1] + '.' + settings.obsIP[2] + '.' + settings.obsIP[3];
  html += "</td></tr><tr><td><br></td></tr>";
  html += "<tr bgcolor=\"#142839\"style=\"color:#ffffff;font-size:.8em;\"><td colspan=\"3\"><h2>&nbsp;Settings:</h2></td></tr><tr><td><br></td></tr><form action=\"/save\"method=\"post\"><tr><td>Tally Light name: </td><td><input type=\"text\"size=\"30\"maxlength=\"30\"name=\"tName\"value=\"";
#if ESP32
  html += WiFi.getHostname();
#else
  html += WiFi.hostname();
#endif
  html += "\"required/></td></tr><tr><td><br></td></tr><tr><td>OBS Source Name: </td><td><input type =\"text\"size=\"30\"maxlength=\"30\"name=\"source\"value=\"";
  html += settings.sourceName;
  html += "\"required/></td></tr><tr><tr><td>OBS IP: </td><td><input class=\"IP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"aIP1\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.obsIP[0];
  html += "\"required/>. <input class=\"IP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"aIP2\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.obsIP[1];
  html += "\"required/>. <input class=\"IP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"aIP3\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.obsIP[2];
  html += "\"required/>. <input class=\"IP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"aIP4\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.obsIP[3];
  html += "\"required/></tr>";
  html += "<tr><td><br></td></tr><td><br></td></tr><tr><td>Tally Light mode (LED 1):&nbsp;</td><td><select name=\"tModeLED1\"><option value=\"";
  html += (String)MODE_NORMAL + "\"";
  if (settings.tallyModeLED1 == MODE_NORMAL)
    html += "selected";
  html += ">Normal</option><option value=\"";
  html += (String)MODE_PREVIEW_STAY_ON + "\"";
  if (settings.tallyModeLED1 == MODE_PREVIEW_STAY_ON)
    html += "selected";
  html += ">Preview stay on</option><option value=\"";
  html += (String)MODE_PROGRAM_ONLY + "\"";
  if (settings.tallyModeLED1 == MODE_PROGRAM_ONLY)
    html += "selected";
  html += ">Program only</option><option value=\"";
  html += (String)MODE_ON_AIR + "\"";
  if (settings.tallyModeLED1 == MODE_ON_AIR)
    html += "selected";
  html += ">On Air</option></select></td></tr><tr><td>Tally Light mode (LED 2):</td><td><select name=\"tModeLED2\"><option value=\"";
  html += (String)MODE_NORMAL + "\"";
  if (settings.tallyModeLED2 == MODE_NORMAL)
    html += "selected";
  html += ">Normal</option><option value=\"";
  html += (String)MODE_PREVIEW_STAY_ON + "\"";
  if (settings.tallyModeLED2 == MODE_PREVIEW_STAY_ON)
    html += "selected";
  html += ">Preview stay on</option><option value=\"";
  html += (String)MODE_PROGRAM_ONLY + "\"";
  if (settings.tallyModeLED2 == MODE_PROGRAM_ONLY)
    html += "selected";
  html += ">Program only</option><option value=\"";
  html += (String)MODE_ON_AIR + "\"";
  if (settings.tallyModeLED2 == MODE_ON_AIR)
    html += "selected";
  html += ">On Air</option></select></td></tr><tr><td> Led brightness: </td><td><input type=\"number\"size=\"5\"min=\"0\"max=\"255\"name=\"ledBright\"value=\"";
  html += settings.ledBrightness;
  html += "\"required/></td></tr><tr><td><br></td></tr><tr><td>Amount of Neopixels:</td><td><input type=\"number\"size=\"5\"min=\"0\"max=\"1000\"name=\"neoPxAmount\"value=\"";
  html += settings.neopixelsAmount;
  html += "\"required/></td></tr><tr><td>Neopixel status LED: </td><td><select name=\"neoPxStatus\"><option value=\"";
  html += (String)NEOPIXEL_STATUS_FIRST + "\"";
  if (settings.neopixelStatusLEDOption == NEOPIXEL_STATUS_FIRST)
    html += "selected";
  html += ">First LED</option><option value=\"";
  html += (String)NEOPIXEL_STATUS_LAST + "\"";
  if (settings.neopixelStatusLEDOption == NEOPIXEL_STATUS_LAST)
    html += "selected";
  html += ">Last LED</option><option value=\"";
  html += (String)NEOPIXEL_STATUS_NONE + "\"";
  if (settings.neopixelStatusLEDOption == NEOPIXEL_STATUS_NONE)
    html += "selected";
  html += ">None</option></select></td></tr><tr><td> Neopixel brightness: </td><td><input type=\"number\"size=\"5\"min=\"0\"max=\"255\"name=\"neoPxBright\"value=\"";
  html += settings.neopixelBrightness;
  html += "\"required/></td></tr><tr><td><br></td></tr><tr><td>Network name(SSID): </td><td><input type =\"text\"size=\"30\"maxlength=\"30\"name=\"ssid\"value=\"";
  html += getSSID();
  html += "\"required/></td></tr><tr><td>Network password: </td><td><input type=\"password\"size=\"30\"maxlength=\"30\"name=\"pwd\"pattern=\"^$|.{8,32}\"value=\"";
  if (WiFi.isConnected())  //As a minimum security meassure, to only send the wifi password if it's currently connected to the given network.
    html += WiFi.psk();
  html += "\"/></td></tr><tr><td><br></td></tr><tr><td>Use static IP: </td><td><input type=\"hidden\"id=\"staticIPHidden\"name=\"staticIP\"value=\"false\"/><input id=\"staticIP\"type=\"checkbox\"name=\"staticIP\"value=\"true\"onchange=\"toggleStaticIPFields()\"";
  if (settings.staticIP)
    html += "checked";
  html += "/></td></tr><tr><td>" + (String)DISPLAY_NAME + " IP: </td><td><input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"tIP1\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallyIP[0];
  html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"tIP2\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallyIP[1];
  html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"tIP3\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallyIP[2];
  html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"tIP4\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallyIP[3];
  html += "\"required/></td></tr><tr><td>Subnet mask: </td><td><input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"mask1\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallySubnetMask[0];
  html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"mask2\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallySubnetMask[1];
  html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"mask3\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallySubnetMask[2];
  html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"mask4\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallySubnetMask[3];
  html += "\"required/></td></tr><tr><td>Gateway: </td><td><input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"gate1\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallyGateway[0];
  html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"gate2\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallyGateway[1];
  html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"gate3\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallyGateway[2];
  html += "\"required/>. <input class=\"tIP\"type=\"text\"size=\"3\"maxlength=\"3\"name=\"gate4\"pattern=\"\\d{0,3}\"value=\"";
  html += settings.tallyGateway[3];
  html += "\"required/></td></tr>";
  html += "<tr><td><br></td></tr><tr><td/><td style=\"float: right;\"><input type=\"submit\"value=\"Save Changes\"/></td></tr></form><tr bgcolor=\"#cccccc\"style=\"font-size: .8em;\"><td colspan=\"3\"><p>&nbsp;&copy; 2023 <a href=\"https://github.com/cjb-tech/\" target=\"_blank\">CJB Tech Team</a></p><p>&nbsp;Based on <a href=\"https://aronhetlam.github.io/ATEM_tally_light_with_ESP8266/\" target=\"_blank\">Wireless ATEM tally lights by aronhetlam</a></p></td></tr></table></body></html>";
  server.send(200, "text/html", html);
}

//Save new settings from client in EEPROM and restart the ESP8266 module
void handleSave() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/html", "<!DOCTYPE html><html><head><meta charset=\"ASCII\"><meta name=\"viewport\"content=\"width=device-width, initial-scale=1.0\"><title>Tally Light setup</title></head><body style=\"font-family:Verdana;\"><table bgcolor=\"#777777\"border=\"0\"width=\"100%\"cellpadding=\"1\"style=\"color:#ffffff;font-size:.8em;\"><tr><td><h1>&nbsp;" + (String)DISPLAY_NAME + " setup</h1></td></tr></table><br>Request without posting settings not allowed</body></html>");
  } else {
    String ssid;
    String pwd;
    bool change = false;
    for (uint8_t i = 0; i < server.args(); i++) {
      change = true;
      String var = server.argName(i);
      String val = server.arg(i);

      if (var == "tName") {
        val.toCharArray(settings.tallyName, (uint8_t)32);
      } else if (var == "source") {
        val.toCharArray(settings.sourceName, (uint8_t)32);
      } else if (var == "tModeLED1") {
        settings.tallyModeLED1 = val.toInt();
      } else if (var == "tModeLED2") {
        settings.tallyModeLED2 = val.toInt();
      } else if (var == "ledBright") {
        settings.ledBrightness = val.toInt();
      } else if (var == "neoPxAmount") {
        settings.neopixelsAmount = val.toInt();
      } else if (var == "neoPxStatus") {
        settings.neopixelStatusLEDOption = val.toInt();
      } else if (var == "neoPxBright") {
        settings.neopixelBrightness = val.toInt();
      } else if (var == "ssid") {
        ssid = String(val);
      } else if (var == "pwd") {
        pwd = String(val);
      } else if (var == "staticIP") {
        settings.staticIP = (val == "true");
      } else if (var == "tIP1") {
        settings.tallyIP[0] = val.toInt();
      } else if (var == "tIP2") {
        settings.tallyIP[1] = val.toInt();
      } else if (var == "tIP3") {
        settings.tallyIP[2] = val.toInt();
      } else if (var == "tIP4") {
        settings.tallyIP[3] = val.toInt();
      } else if (var == "mask1") {
        settings.tallySubnetMask[0] = val.toInt();
      } else if (var == "mask2") {
        settings.tallySubnetMask[1] = val.toInt();
      } else if (var == "mask3") {
        settings.tallySubnetMask[2] = val.toInt();
      } else if (var == "mask4") {
        settings.tallySubnetMask[3] = val.toInt();
      } else if (var == "gate1") {
        settings.tallyGateway[0] = val.toInt();
      } else if (var == "gate2") {
        settings.tallyGateway[1] = val.toInt();
      } else if (var == "gate3") {
        settings.tallyGateway[2] = val.toInt();
      } else if (var == "gate4") {
        settings.tallyGateway[3] = val.toInt();
      } else if (var == "aIP1") {
        settings.obsIP[0] = val.toInt();
      } else if (var == "aIP2") {
        settings.obsIP[1] = val.toInt();
      } else if (var == "aIP3") {
        settings.obsIP[2] = val.toInt();
      } else if (var == "aIP4") {
        settings.obsIP[3] = val.toInt();
      }
    }

    if (change) {
      EEPROM.put(0, settings);
      EEPROM.commit();

      server.send(200, "text/html", (String) "<!DOCTYPE html><html><head><meta charset=\"ASCII\"><meta name=\"viewport\"content=\"width=device-width, initial-scale=1.0\"><title>Tally Light setup</title></head><body><table bgcolor=\"#777777\"border=\"0\"width=\"100%\"cellpadding=\"1\"style=\"font-family:Verdana;color:#ffffff;font-size:.8em;\"><tr><td><h1>&nbsp;" + (String)DISPLAY_NAME + " setup</h1></td></tr></table><br>Settings saved successfully.</body></html>");

      // Delay to let data be saved, and the response to be sent properly to the client
      server.close();  // Close server to flush and ensure the response gets to the client
      delay(100);

      // Change into STA mode to disable softAP
      WiFi.mode(WIFI_STA);
      delay(100);  // Give it time to switch over to STA mode (this is important on the ESP32 at least)

      if (ssid && pwd) {
        WiFi.persistent(true);  // Needed by ESP8266
        // Pass in 'false' as 5th (connect) argument so we don't waste time trying to connect, just save the new SSID/PSK
        // 3rd argument is channel - '0' is default. 4th argument is BSSID - 'NULL' is default.
        WiFi.begin(ssid.c_str(), pwd.c_str(), 0, NULL, false);
      }

      //Delay to apply settings before restart
      delay(100);
      ESP.restart();
    }
  }
}

//Send 404 to client in case of invalid webpage being requested.
void handleNotFound() {
  server.send(404, "text/html", "<!DOCTYPE html><html><head><meta charset=\"ASCII\"><meta name=\"viewport\"content=\"width=device-width, initial-scale=1.0\"><title>" + (String)DISPLAY_NAME + " setup</title></head><body style=\"font-family:Verdana;\"><table bgcolor=\"#777777\"border=\"0\"width=\"100%\"cellpadding=\"1\"style=\"color:#ffffff;font-size:.8em;\"><tr><td><h1>&nbsp Tally Light setup</h1></td></tr></table><br>404 - Page not found</body></html>");
}

String getSSID() {
#if ESP32
  wifi_config_t conf;
  esp_wifi_get_config(WIFI_IF_STA, &conf);
  return String(reinterpret_cast<const char *>(conf.sta.ssid));
#else
  return WiFi.SSID();
#endif
}