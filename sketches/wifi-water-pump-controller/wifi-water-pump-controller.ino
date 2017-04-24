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
int DEFAULT_RUNTIME = 10;
long max_runtime;
boolean resetFlag=false;
boolean debug=true;
boolean timeover;
int eeAddress = 0;
int liquidLevelSensorReadIn = 0;
int liquidLevelSensorReadInThreshold = 950;
String switch1state;

boolean LIQUID_LEVEL_OK = false;

String capailities = "{\"name\":\"HMU-PC-001\",\"description\":\"WATER PUMP CONTROLLER WITH WATER LEVEL SAFETY\",\"devices\":{\"SWITCH1\":{\"get\":\"\/switch\/1\",\"set\":\"\/switch\/1\/set\",\"runtime\":{\"get\":\"\/switch\/1\/runtime\",\"set\":\"\/switch\/1\/runtime\/set?time={value}\"},\"type\":\"switch\",\"states\":[\"on\",\"off\"]},\"sensor1\":{\"index\":1,\"name\":\"LIQUID_LEVEL_SENSOR\",\"get\":\"\/sensor\/1\/get\",\"type\":\"magnetic_float_switch\",\"states\":[\"true\",\"false\"]}},\"global\":{\"actions\":{\"get\":\"\/switch\/all\",\"reset\":\"\/reset\",\"info\":\"\/\"}}}";

struct Settings {
   int relay_1;
   int relay_runtime;
   int led;
   int lastupdate;
   long relay_start;
   long relay_stop;
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


void getSwitchRuntime()
{
  int runtime;

  readSettings();

  runtime = conf.relay_runtime;

  if(runtime > 0)
  {
    server->send(200, "text/plain", "RUNTIME=" + String(runtime));
  }
  else
  {
    server->send(400, "text/plain", "Invalid runtime value");
  }
}




void setSwitchRuntime()
{
  int runtime;

  if(server->hasArg("time"))
  {
    runtime = String(server->arg("time")).toInt();

    if(runtime > 0)
    {
      conf.relay_runtime = runtime;
      
      server->send(200, "text/plain", "RUNTIME=" + String(conf.relay_runtime));
      writeSettings();
    }
    else
    {
      server->send(400, "text/plain", "Invalid runtime value");
    }
  }
  else
  {
    server->send(400, "text/plain", "No value provided");
  }
  
  
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
  conf.relay_start = millis();
  digitalWrite(RELAY1, LOW);
}


void relayOff()
{
  conf.relay_1=0;
  conf.relay_stop = millis();
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

  if(conf.relay_1 == 1)
  {
    max_runtime = conf.relay_runtime * 1000;
    timeover = ((millis() - conf.relay_start) > max_runtime);
    
    if(!LIQUID_LEVEL_OK || timeover)
    {
      relayOff();
    }
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


  server->on("/switch/1", readSwitchA);
  server->on("/switch/1/set", toggleSwitchA);
  server->on("/switch/1/set/on", switchAOn);
  server->on("/switch/1/set/off", switchAOff);
  server->on("/switch/1/runtime", getSwitchRuntime);
  server->on("/switch/1/runtime/set", setSwitchRuntime);
  server->on("/switch/all", readAllSwitches);
  
  server->onNotFound(handleNotFound);
  server->begin();
  
  debugPrint("HTTP server started");
  debugPrint(String(WiFi.localIP()));
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
      delay(5);
      
      server->handleClient();
    }
}





void initSettings()
{
  readSettings();

  if(conf.relay_runtime <= 0)
  {
    conf.relay_runtime = DEFAULT_RUNTIME;
    writeSettings();
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
  conf.lastupdate = millis();
  
  EEPROM.write(eeAddress, conf.relay_runtime);
  eeAddress++;
  EEPROM.write(eeAddress, conf.lastupdate);
  EEPROM.commit();
  
  debugPrint("Conf saved");
  debugPrint(String(conf.relay_runtime));
  debugPrint(String(conf.lastupdate));
}




void readSettings() 
{
  eeAddress = 0;
  
  conf.relay_runtime = EEPROM.read(eeAddress);
  eeAddress++;
  conf.lastupdate = EEPROM.read(eeAddress);

  debugPrint("Conf read");
  debugPrint(String(conf.relay_runtime));
  debugPrint(String(conf.lastupdate));
}



void debugPrint(String message){
  if(debug){
    Serial.println(message);
  }
}


