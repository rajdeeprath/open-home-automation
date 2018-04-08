#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"         //https://github.com/tzapu/WiFiManager
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <QueueArray.h>
#include <SoftwareSerial.h>


const String NAME="APT-PC-001";
const long CONSECUTIVE_NOTIFICATION_DELAY = 5000;

const String capailities = "";

struct Settings {
   int low;
   int medium;
   int high;
   int pump;
   int maintainence;
   int fault;
   long lastupdate;
   int reset = 0;
   int notify = 1;
   int endpoint_length;
   char endpoint[80] = "";
};



struct Notification {
   int low;
   int medium;
   int high;
   int pump;
   int maintainence;
   int fault;
   char message[80] = "";
   long queue_time;
   long send_time;
};

Settings conf = {};
QueueArray <Notification> queue;

WiFiManager wifiManager;
std::unique_ptr<ESP8266WebServer> server;
SoftwareSerial SoftSerial(13, 15);

HTTPClient http;
boolean posting;
boolean inited = false;
long last_notify = 0;
int eeAddress = 0;
boolean debug = true;
char inChar; // Where to store the character read
String content;
char sendBuffer[100];
char recvBuffer[100];


/**
 * Handle root visit
 */
void handleRoot() {
  server->send(200, "application/json", capailities);
}




/**
 * Handle reset request
 */
void handleReset() 
{
  conf.reset = 1;
  writeSettings();
  server->send(200, "text/plain", "Resetting in 5 seconds");
}




/**
 * Handle non existent path
 */
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server->uri();
  message += "\nMethod: ";
  message += (server->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server->args();
  message += "\n";
  for (uint8_t i = 0; i < server->args(); i++) {
    message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
  }
  server->send(404, "text/plain", message);
}




/**
 * Gets the http(s) Notification permission
 */
void getNotify()
{
  readSettings();

  int notify = conf.notify;
  server->send(200, "text/plain", "NOTIFY=" + String(notify));
}




/**
 * Sets the http(s) Notification permission
 */
void setNotify()
{
  int notify;

  if (server->hasArg("notify"))
  {
    notify = String(server->arg("notify")).toInt();

    if (notify == 0 || notify == 1)
    {
      conf.notify = notify;
      writeSettings();
      server->send(200, "text/plain", "NOTIFY=" + String(conf.notify));      
    }
    else
    {
      server->send(400, "text/plain", "Invalid notify value");
    }
  }
  else
  {
    server->send(400, "text/plain", "No value provided");
  }
}



/**
 * Gets the http(s) Notification url endpoint
 */
void getNotifyURL()
{
  String url;

  readSettings();

  url = conf.endpoint;

  if (url.length() < 4)
  {
    server->send(400, "text/plain", "Invalid url value");
  }
  else
  {
    server->send(200, "text/plain", "URL=" + url);
  }
}




/**
 * Sets the http(s) Notification url endpoint
 */
void setNotifyURL()
{
  String url;

  if (server->hasArg("url"))
  {
    url = String(server->arg("url"));

    if (url.length() < 4)
    {
      server->send(400, "text/plain", "Invalid url value");
    }
    else
    {
      char tmp[url.length() + 1];
      url.toCharArray(tmp, url.length() + 1);

      conf.endpoint_length = url.length();

      memset(conf.endpoint, 0, sizeof(conf.endpoint));
      strncpy(conf.endpoint, tmp, strlen(tmp));

      writeSettings();
      
      server->send(200, "text/plain", "url=" + url);
    }
  }
  else
  {
    server->send(400, "text/plain", "No value provided");
  }
}



void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  SoftSerial.begin(115200);
  
  queue.setPrinter (Serial); 
  
  // Check for reset and do reset routine  
  readSettings();  
  if(conf.reset == 1){
    debugPrint("Reset flag detected!");    
    doReset();
  }

 
  char APNAME[NAME.length() + 1];
  NAME.toCharArray(APNAME, NAME.length() + 1);
  wifiManager.autoConnect(APNAME, "iot@123!");

  //if you get here you have connected to the WiFi
  debugPrint("connected...yeey :)");
  
  server.reset(new ESP8266WebServer(WiFi.localIP(), 80));

  server->on("/", handleRoot);
  server->on("/reset", handleReset);
  server->on("/notify", getNotify);
  server->on("/notify/set", setNotify);
  server->on("/notify/url", getNotifyURL);
  server->on("/notify/url/set", setNotifyURL);
  
  server->onNotFound(handleNotFound);
  server->begin();
  
  debugPrint("HTTP server started");
  debugPrint(String(WiFi.localIP()));

}



void loop() {

  if(!inited){
   inited = true;
   initSettings();
  }

  if (conf.reset == 1)
  {
    delay(5000);
    ESP.restart();
  }
  else
  {
    readSerial();
    delay(3);
    server->handleClient();
  }
}



void readSerial()
{
  while(SoftSerial.available())
  {
     inChar = SoftSerial.read(); // Read a character
     content.concat(inChar);
  }

  if(content.length()>1)
  {
    // |TYPE:DATA;PARAMS:SOUND=0&LIGHT=1&MOTION=1&KEYPRESS=?|
    debugPrint(content);    
    content = "";
  }
}




/**
 * Resets the state of the device by resetting configuration data and erasing eeprom
 */
void doReset()
{
  conf.low=0;
  conf.medium=0;
  conf.high = 0;
  conf.maintainence = 0;
  conf.fault = 0;
  conf.lastupdate = 0;
  conf.endpoint_length = 0;
  conf.reset = 0;
  conf.notify = 0;
  memset(conf.endpoint, 0, sizeof(conf.endpoint));
  
  eraseSettings();   
  delay(1000);
  
  wifiManager.resetSettings();
}




/**
 * Initializing
 */
void initSettings()
{
  readSettings();

  if(conf.lastupdate <= 0)
  {
    debugPrint("Setting defaults");
    
    String url = "0.0.0.0";
    char tmp[url.length() + 1];
    url.toCharArray(tmp, url.length() + 1);

    conf.endpoint_length = url.length();
    memset(conf.endpoint, 0, sizeof(conf.endpoint));
    strncpy(conf.endpoint, tmp, strlen(tmp));

    conf.lastupdate = millis();
    conf.notify = 0;
  }  

  conf.low=0;
  conf.medium=0;
  conf.high = 0;
  conf.maintainence = 0;
  conf.fault = 0;

  writeSettings();
}




/**
 * Erases eeprom of all settings
 */
void eraseSettings()
{
  EEPROM.begin(512);
  
  debugPrint("Erasing eeprom...");
  
  for (int i = 0; i < 512; i++){
    EEPROM.write(i, 0);
  }

  EEPROM.commit();
  EEPROM.end();
}




/**
 * Writes active configuration  to eeprom
 */
void writeSettings() 
{
  EEPROM.begin(512);
  
  eeAddress = 0;
  conf.lastupdate = millis();

  EEPROM.write(eeAddress, conf.low);
  eeAddress++;  
  EEPROM.write(eeAddress, conf.medium);
  eeAddress++;
  EEPROM.write(eeAddress, conf.high);
  eeAddress++;
  EEPROM.write(eeAddress, conf.maintainence);
  eeAddress++;
  EEPROM.write(eeAddress, conf.fault);
  eeAddress++;
  EEPROM.write(eeAddress, conf.reset);
  eeAddress++;
  EEPROM.write(eeAddress, conf.notify);
  eeAddress++;
  EEPROM.write(eeAddress, conf.endpoint_length);
  eeAddress++;
  writeEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  EEPROM.commit();
  EEPROM.end();
  
  debugPrint("Conf saved");
}


/**
 * Write string to eeprom
 */
void writeEEPROM(int startAdr, int len, char* writeString) {
  //yield();
  for (int i = 0; i < len; i++) {
    EEPROM.write(startAdr + i, writeString[i]);
  }
}




/**
 * Reads last written configuration object from eeprom
 */
void readSettings() 
{
  EEPROM.begin(512);
  
  eeAddress = 0;
  
  conf.low = EEPROM.read(eeAddress);
  eeAddress++;
  conf.medium = EEPROM.read(eeAddress);
  eeAddress++;
  conf.high = EEPROM.read(eeAddress);
  eeAddress++;
  conf.maintainence = EEPROM.read(eeAddress);
  eeAddress++;
  conf.fault = EEPROM.read(eeAddress);
  eeAddress++;
  conf.lastupdate = EEPROM.read(eeAddress);
  eeAddress++;
  conf.reset = EEPROM.read(eeAddress);
  eeAddress++;
  conf.notify = EEPROM.read(eeAddress);
  eeAddress++;
  conf.endpoint_length = EEPROM.read(eeAddress);

  eeAddress++;
  readEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  EEPROM.end();

  debugPrint("Conf read");
}



/**
 * Reads string from eeprom
 */
void readEEPROM(int startAdr, int maxLength, char* dest) {

  for (int i = 0; i < maxLength; i++) {
    dest[i] = char(EEPROM.read(startAdr + i));
  }
}




/**
 * Prints message to serial
 */
void debugPrint(String message){
  if(debug){
    Serial.println(message);
  }
}
