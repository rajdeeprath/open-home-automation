#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"         //https://github.com/tzapu/WiFiManager
#include <EEPROM.h>


const String NAME="HMU-SB-001";

const int RELAY1=4;
const int RELAY2=5;
const int RELAY3=13;

boolean resetFlag=false;
boolean debug=true;

int eeAddress = 0;
String switch1state;
String switch2state;
String switch3state;

String capailities = "{\"name\":\"HMU-SB-001\",\"devices\":{\"SWITCH1\":{\"get\":\"\/switch\/1\",\"set\":\"\/switch\/1\/set\",\"type\":\"switch\",\"states\":[\"on\",\"off\"]},\"SWITCH2\":{\"get\":\"\/switch\/2\",\"set\":\"\/switch\/2\/set\",\"type\":\"switch\",\"states\":[\"on\",\"off\"]},\"SWITCH3\":{\"get\":\"\/switch\/3\",\"set\":\"\/switch\/3\/set\",\"type\":\"switch\",\"states\":[\"on\",\"off\"]}},\"global\":{\"actions\":{\"get\":\"\/switch\/all\",\"reset\":\"\/reset\",\"info\":\"\/\"}}}";

struct Settings {
   int relay_1;
   int relay_2;
   int relay_3;
   long timestamp;
};

Settings conf = {};

std::unique_ptr<ESP8266WebServer> server;

void handleRoot() {
  server->send(200, "application/json", capailities);
}


void handleReset() 
{
  resetFlag=true;
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



void readAllSwitches()
{
  if(conf.relay_1 == 0)
  {
    switch1state="STATE=OFF";
  }
  else
  {
    switch1state="STATE=ON";
  }


  if(conf.relay_2 == 0)
  {
    switch2state="STATE=OFF";
  }
  else
  {
    switch2state="STATE=ON";
  }


  if(conf.relay_3 == 0)
  {
    switch3state="STATE=OFF";
  }
  else
  {
    switch3state="STATE=ON";
  }

  server->send(200, "text/plain", "SWITCH1:" + switch1state + ";SWITCH2:" + switch2state + ";SWITCH3:" + switch3state);
}



void readSwitchC()
{
  //readSettings();

  if(conf.relay_3 == 0)
  {
    switch3state="STATE=OFF";
  }
  else
  {
    switch3state="STATE=ON";
  }

  server->send(200, "text/plain", switch3state);
}



void toggleSwitchC()
{
  readSettings();

  if(conf.relay_3 == 0)
  {
    conf.relay_3=1;
    digitalWrite(RELAY3, LOW);
    switch3state="STATE=ON";
  }
  else
  {
    conf.relay_3=0;
    digitalWrite(RELAY3, HIGH);
    switch3state="STATE=OFF";
  }

  server->send(200, "text/plain", switch3state);
  writeSettings();
}




void switchCOn()
{
  conf.relay_3=1;
  digitalWrite(RELAY3, LOW);

  server->send(200, "text/plain", "STATE=ON");
  writeSettings();
}




void switchCOff()
{
  conf.relay_3=0;
  digitalWrite(RELAY3, HIGH);

  server->send(200, "text/plain", "STATE=OFF");
  writeSettings();
}



void readSwitchB()
{
  //readSettings();

  if(conf.relay_2 == 0)
  {
    switch2state="STATE=OFF";
  }
  else
  {
    switch2state="STATE=ON";
  }

  server->send(200, "text/plain", switch2state);
}



void toggleSwitchB()
{
  readSettings();

  if(conf.relay_2 == 0)
  {
    conf.relay_2=1;
    digitalWrite(RELAY2, LOW);
    switch2state="STATE=ON";
  }
  else
  {
    conf.relay_2=0;
    digitalWrite(RELAY2, HIGH);
    switch2state="STATE=OFF";
  }

  server->send(200, "text/plain", switch2state);
  writeSettings();
}




void switchBOn()
{
  conf.relay_2=1;
  digitalWrite(RELAY2, LOW);

  server->send(200, "text/plain", "STATE=ON");
  writeSettings();
}




void switchBOff()
{
  conf.relay_2=0;
  digitalWrite(RELAY2, HIGH);

  server->send(200, "text/plain", "STATE=OFF");
  writeSettings();
}




void readSwitchA()
{
  //readSettings();

  if(conf.relay_1 == 0)
  {
    switch1state="STATE=OFF";
  }
  else
  {
    switch1state="STATE=ON";
  }

  server->send(200, "text/plain", switch1state);
}


void toggleSwitchA()
{
  readSettings();

  if(conf.relay_1 == 0)
  {
    conf.relay_1=1;
    digitalWrite(RELAY1, LOW);
    switch1state="STATE=ON";
  }
  else
  {
    conf.relay_1=0;
    digitalWrite(RELAY1, HIGH);
    switch1state="STATE=OFF";
  }

  server->send(200, "text/plain", switch1state);
  writeSettings();
}




void switchAOn()
{
  conf.relay_1=1;
  digitalWrite(RELAY1, LOW);

  server->send(200, "text/plain", "STATE=ON");
  writeSettings();
}




void switchAOff()
{
  conf.relay_1=0;
  digitalWrite(RELAY1, HIGH);

  server->send(200, "text/plain", "STATE=OFF");
  writeSettings();
}



void restoreSystem()
{
  readSettings();

  if(conf.relay_1 == 0)
  {
    digitalWrite(RELAY1, HIGH);
  }
  else
  {
    digitalWrite(RELAY1, LOW);
  }


  if(conf.relay_2 == 0)
  {
    digitalWrite(RELAY2, HIGH);
  }
  else
  {
    digitalWrite(RELAY2, LOW);
  }


  if(conf.relay_3 == 0)
  {
    digitalWrite(RELAY3, HIGH);
  }
  else
  {
    digitalWrite(RELAY3, LOW);
  }
}



void setup() {

  pinMode(RELAY1, OUTPUT);
  digitalWrite(RELAY1, HIGH);
  
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY2, HIGH);
  
  pinMode(RELAY3, OUTPUT);
  digitalWrite(RELAY3, HIGH);

  conf.relay_1 = 0;
  conf.relay_2 = 0;
  conf.relay_3 = 0;
  conf.timestamp = millis();


  // put your setup code here, to run once:
  Serial.begin(115200);

  EEPROM.begin(512);
  restoreSystem();
  
  WiFiManager wifiManager;
  wifiManager.autoConnect();


  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  
  server.reset(new ESP8266WebServer(WiFi.localIP(), 80));

  server->on("/", handleRoot);
  server->on("/reset", handleReset);


  server->on("/switch/1", readSwitchA);
  server->on("/switch/1/set", toggleSwitchA);
  server->on("/switch/1/set/on", switchAOn);
  server->on("/switch/1/set/off", switchAOff);


  server->on("/switch/2", readSwitchB);
  server->on("/switch/2/set", toggleSwitchB);
  server->on("/switch/2/set/on", switchBOn);
  server->on("/switch/2/set/off", switchBOff);


  server->on("/switch/3", readSwitchC);
  server->on("/switch/3/set", toggleSwitchC);
  server->on("/switch/3/set/on", switchCOn);
  server->on("/switch/3/set/off", switchCOff);


  server->on("/switch/all", readAllSwitches);

  /*
  server->on("/inline", []() {
    server->send(200, "text/plain", "this works as well");
  });
  */
  
  server->onNotFound(handleNotFound);

  server->begin();
  Serial.println("HTTP server started");
  Serial.println(WiFi.localIP());
}

void loop() {

    if(resetFlag)
    {
      resetFlag = false;
      
      delay(5000);
      
      WiFiManager wifiManager;
      wifiManager.resetSettings();

      eraseSettings(); 
      ESP.restart();
    }
    else
    {
      server->handleClient();
    }
}




void eraseSettings()
{
  EEPROM.begin(512);

  for (int i = 0; i < 512; i++)
  EEPROM.write(i, 0);

  EEPROM.end();
}



void writeSettings() 
{
  eeAddress = 0;
  conf.timestamp = millis();
  
  EEPROM.write(eeAddress, conf.relay_1);
  eeAddress++;
  EEPROM.write(eeAddress, conf.relay_2);
  eeAddress++;
  EEPROM.write(eeAddress, conf.relay_3);
  eeAddress++;
  EEPROM.write(eeAddress, conf.timestamp);
  EEPROM.commit();
  
  debugPrint("Conf saved");

  debugPrint(String(conf.relay_1));
  debugPrint(String(conf.relay_2));
  debugPrint(String(conf.relay_3));
  debugPrint(String(conf.timestamp));
}




void readSettings() 
{
  eeAddress = 0;
  
  conf.relay_1 = EEPROM.read(eeAddress);
  eeAddress++;
  conf.relay_2 = EEPROM.read(eeAddress);
  eeAddress++;
  conf.relay_3 = EEPROM.read(eeAddress);
  eeAddress++;
  conf.timestamp = EEPROM.read(eeAddress);
  
  debugPrint(String(conf.relay_1));
  debugPrint(String(conf.relay_2));
  debugPrint(String(conf.relay_3));
  debugPrint(String(conf.timestamp));
}



void debugPrint(String message){
  if(debug){
    Serial.println(message);
  }
}


