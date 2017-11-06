#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"         //https://github.com/tzapu/WiFiManager
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <RCSwitch.h>

#define ADC A0
#define RF_TRANSMIT 4

const String NAME = "HMU-BL-001";
String capailities = "{\"name\":\"HMU-SB-001\",\"devices\":{\"SWITCH1\":{\"get\":\"\/switch\/1\",\"set\":\"\/switch\/1\/set\",\"type\":\"switch\",\"states\":[\"on\",\"off\"]},\"SWITCH2\":{\"get\":\"\/switch\/2\",\"set\":\"\/switch\/2\/set\",\"type\":\"switch\",\"states\":[\"on\",\"off\"]},\"SWITCH3\":{\"get\":\"\/switch\/3\",\"set\":\"\/switch\/3\/set\",\"type\":\"switch\",\"states\":[\"on\",\"off\"]}},\"global\":{\"actions\":{\"get\":\"\/switch\/all\",\"reset\":\"\/reset\",\"info\":\"\/\"}}}";

boolean resetFlag = false;
boolean debug = true;
int eeAddress = 0;
int bell_input;
boolean BELL_ON = false;
boolean canNotify = false;
long lastDetection;
long sinceLastDetection;


RCSwitch mySwitch = RCSwitch();

struct Settings {
  int notify = 1;
  long timestamp;
  int endpoint_length;
  char endpoint[50] = "";
};


Settings conf = {};
std::unique_ptr<ESP8266WebServer> server;


HTTPClient http;
boolean posting;

/**************************************************/

void handleRoot() {
  server->send(200, "application/json", capailities);
}


void handleReset()
{
  resetFlag = true;
  server->send(200, "text/plain", "Resetting in 5 seconds");
}


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



void getBellNotifyURL()
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
    server->send(200, "text/plain", "url=" + url);
  }
}




void setBellNotifyURL()
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
      
      server->send(200, "text/plain", "url=" + url);
      writeSettings();
    }
  }
  else
  {
    server->send(400, "text/plain", "No value provided");
  }
}



void getNotify()
{
  readSettings();

  int notify = conf.notify;

  server->send(200, "text/plain", "notify=" + String(notify));
}




void setNotify()
{
  int notify;

  if (server->hasArg("notify"))
  {
    notify = String(server->arg("notify")).toInt();

    if (notify == 0 || notify == 1)
    {
      conf.notify = notify;
      server->send(200, "text/plain", "notify=" + String(conf.notify));
      writeSettings();
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


void setup()
{
  Serial.begin(9600);

  // start eeprom
  EEPROM.begin(512);
  initSettings();

  WiFiManager wifiManager;
  wifiManager.autoConnect();

  //if you get here you have connected to the WiFi
  debugPrint("connected...yeey :)");

  server.reset(new ESP8266WebServer(WiFi.localIP(), 80));

  server->on("/", handleRoot);
  server->on("/reset", handleReset);

  server->on("/notify", getNotify);
  server->on("/notif/set", setNotify);

  server->on("/notify/url", getBellNotifyURL);
  server->on("/notify/url/set", setBellNotifyURL);

  server->onNotFound(handleNotFound);
  server->begin();

  debugPrint("HTTP server started");
  debugPrint(String(WiFi.localIP()));

  // Transmitter is connected to Arduino Pin #D2 (04)  
  mySwitch.enableTransmit(RF_TRANSMIT);
  
}

void loop() {

  if (resetFlag)
  {
    resetFlag = false;
    delay(5000);

    eraseSettings();

    WiFiManager wifiManager;
    wifiManager.resetSettings();

    ESP.restart();
  }
  else
  {
    checkBell();

    delay(3);
    server->handleClient();
  }
}



void checkBell()
{
   // disable fo rdebugging
  //bell_input = analogRead(ADC);
  bell_input = 600;
  sinceLastDetection = millis() - lastDetection;
  //debugPrint("sinceLastDetection " + String(sinceLastDetection));
  
  // Bell on is valid for 30 seconds only
  if(sinceLastDetection > 30000){
    //debugPrint("Assuming bell off");
    BELL_ON = false;
  }

  canNotify = ((sinceLastDetection > 60000) && (bell_input > 500));
  //debugPrint("canNotify " + String(canNotify));
  
  if(canNotify && !BELL_ON)
  {
    debugPrint("Assuming bell on");
    
    BELL_ON = true;
    lastDetection = millis();

    if(conf.notify == 1)
    {
      // VIA RF
      notifyRF();
      
      // VIA URL
      if(conf.endpoint_length > 4){
        notifyURL();
      }
    }
  }
}



void notifyRF()
{
  debugPrint("Sending transmission");  
  mySwitch.send("0100001001000101010011000100110000100001");
}


void notifyURL()
{
  if(!posting)
  {
    readSettings();  
    
    posting = true;
    debugPrint("Sending url call");    
        
    http.begin(String(conf.endpoint));
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    int httpCode = http.POST("bell=" + String(BELL_ON));
    
    String payload = http.getString();
    
    debugPrint(String(httpCode));
    debugPrint(String(payload));   
    
    http.end();

    posting = false;
  }
}


void initSettings()
{
  sinceLastDetection = millis() ;
  
  readSettings();
  if(conf.endpoint_length <= 0){
      char tmp[] = "";
      strncpy(conf.endpoint, tmp, strlen(tmp));
      writeSettings();
  }
}


void writeSettings()
{
  eeAddress = 0;
  conf.timestamp = millis();

  EEPROM.write(eeAddress, conf.notify);
  eeAddress++;
  EEPROM.write(eeAddress, conf.timestamp);
  eeAddress++;
  EEPROM.write(eeAddress, conf.endpoint_length); 

  eeAddress++;
  writeEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);
  
  EEPROM.commit();

  debugPrint("Conf saved");
  debugPrint(String(conf.notify));
  debugPrint(String(conf.endpoint_length));
  debugPrint(String(conf.endpoint));
  debugPrint(String(conf.timestamp));
}



void writeEEPROM(int startAdr, int len, char* writeString) {
  //yield();
  for (int i = 0; i < len; i++) {
    EEPROM.write(startAdr + i, writeString[i]);
  }
}




void readSettings()
{
  eeAddress = 0;

  conf.notify = EEPROM.read(eeAddress);
  eeAddress++;
  conf.timestamp = EEPROM.read(eeAddress);
  eeAddress++;
  conf.endpoint_length = EEPROM.read(eeAddress);

  eeAddress++;
  readEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  debugPrint("Conf read");
  debugPrint(String(conf.notify));
  debugPrint(String(conf.endpoint_length));
  debugPrint(String(conf.endpoint));
  debugPrint(String(conf.timestamp));
}



void readEEPROM(int startAdr, int maxLength, char* dest) {
  
  for (int i = 0; i < maxLength; i++) {
    dest[i] = char(EEPROM.read(startAdr + i));
  }
}



void eraseSettings()
{
  for (int i = 0; i < 512; i++){
    EEPROM.write(i, 0);
  }
}




void debugPrint(String message) {
  if (debug) {
    Serial.println(message);
  }
}
