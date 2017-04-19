#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"         //https://github.com/tzapu/WiFiManager
#include <EEPROM.h>


const String NAME="HMU-PC-001";

const int RELAY1=4;
const int LIQUID_LEVEL_SENSOR=A0;
const int BEEPER=13;
const int LED=16;

boolean resetFlag=false;
boolean debug=true;

int eeAddress = 0;
int liquidLevelSensorReadIn = 0;
int liquidLevelSensorReadInThreshold = 950;
String switch1state;

boolean LIQUID_LEVEL_OK = false;

String capailities = "{\"name\":\"HMU-PC-001\",\"devices\":{\"SWITCH1\":{\"get\":\"\/switch\/1\",\"set\":\"\/switch\/1\/set\",\"type\":\"switch\",\"states\":[\"on\",\"off\"]},\"global\":{\"actions\":{\"get\":\"\/switch\/all\",\"reset\":\"\/reset\",\"info\":\"\/\"}}}";

struct Settings {
   int relay_1;
   int led;
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

  server->send(200, "text/plain", "SWITCH1:" + switch1state);
}




void readSwitchA()
{
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
  checkAndRespondToRelayConditionSafeGuard();

  if(conf.relay_1 == 0)
  {
    relayOn();
    switch1state="STATE=ON";
  }
  else
  {
    relayOff();
    switch1state="STATE=OFF";
  }

  server->send(200, "text/plain", switch1state);
}




void switchAOn()
{
  checkAndRespondToRelayConditionSafeGuard();
  
  relayOn();
    
  server->send(200, "text/plain", "STATE=ON");
}




void switchAOff()
{
  checkAndRespondToRelayConditionSafeGuard();
  
  relayOff();

  server->send(200, "text/plain", "STATE=OFF");
}



void relayOn()
{
  conf.relay_1=1;
  digitalWrite(RELAY1, LOW);
}


void relayOff()
{
  conf.relay_1=0;
  digitalWrite(RELAY1, HIGH);
}


void ledOn()
{
  conf.led=1;
  digitalWrite(LED, HIGH); 
}


void ledOff()
{
  conf.led=0;
  digitalWrite(LED, LOW); 
}



void restoreSystem()
{
  readSettings();
}




void checkAndRespondToRelayConditionSafeGuard()
{
  if(!LIQUID_LEVEL_OK)
  {
     server->send(400, "text/plain", "LIQUID_LEVEL_OK=FALSE");
     return;
  }
}



void relayConditionSafeGuard()
{
  liquidLevelSensorReadIn = analogRead(LIQUID_LEVEL_SENSOR);
  if(liquidLevelSensorReadIn >= liquidLevelSensorReadInThreshold) // open magnetic switch
  {
    if(LIQUID_LEVEL_OK)
    {
      ledOn();
      LIQUID_LEVEL_OK = false;
    } 
  }
  else // closed magnetic switch
  {
    if(!LIQUID_LEVEL_OK)
    {
      ledOff();
      LIQUID_LEVEL_OK = true;
    }
  }


  /****************************************************************/

  if(!LIQUID_LEVEL_OK && conf.relay_1 == 1)
  {
    relayOff();
  }
}



void setup() {

  conf.relay_1=0;
  conf.led=1;

  pinMode(RELAY1, OUTPUT);
  digitalWrite(RELAY1, HIGH);

  pinMode(LIQUID_LEVEL_SENSOR, INPUT);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);  

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

  server->on("/switch/all", readAllSwitches);
  
  server->onNotFound(handleNotFound);

  server->begin();
  Serial.println("HTTP server started");
  Serial.println(WiFi.localIP());
}

void loop() 
{
    if(resetFlag)
    {
      resetFlag = false;
      
      delay(5000);
      
      WiFiManager wifiManager;
      wifiManager.resetSettings();

      eraseSettings(); 
      ESP.reset();
    }
    else
    {
      relayConditionSafeGuard();
      delay(3);
      
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
}




void readSettings() 
{
  eeAddress = 0;
}



void debugPrint(String message){
  if(debug){
    Serial.println(message);
  }
}


