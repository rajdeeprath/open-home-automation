#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"         //https://github.com/tzapu/WiFiManager
#include <EEPROM.h>

#define RELAY1 4 
#define RELAY2 5 
#define RELAY1_READER 12
#define RELAY2_READER 14
#define LIQUID_LEVEL_SENSOR A0 
#define LED 16 
#define PUMP_SENSOR 15

const String NAME="HMU-PC-001";

int DEFAULT_RUNTIME = 10;
long max_runtime;
long system_start_time;
long wait_time = 5000;
boolean resetFlag=false;
boolean debug=true;
boolean inited = false;
boolean timeover;
int eeAddress = 0;
int liquidLevelSensorReadIn = 0;
int liquidLevelSensorReadInThreshold = 950;
String switch1state;
boolean LIQUID_LEVEL_OK = true;
boolean RELAY_ON;

String capailities = "{\"name\":\"" + NAME + "\",\"devices\":{\"name\":\"Irrigation Pump Controller\",\"actions\":{\"getSwitch\":{\"method\":\"get\",\"path\":\"\/switch\/1\"},\"toggleSwitch\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\"},\"setSwitchOn\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\/on\"},\"setSwitchOff\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\/off\"},\"getAllSwitches\":{\"method\":\"get\",\"path\":\"\/switch\/all\"},\"getRuntime\":{\"method\":\"get\",\"path\":\"\/switch\/1\/runtime\"},\"setRuntime\":{\"method\":\"get\",\"path\":\"\/switch\/1\/runtime\",\"params\":[{\"name\":\"time\",\"type\":\"Number\",\"values\":\"60, 80, 100 etc\"}]}}},\"global\":{\"actions\":{\"getNotify\":{\"method\":\"get\",\"path\":\"\/notify\"},\"setNotify\":{\"method\":\"get\",\"path\":\"\/notify\/set\",\"params\":[{\"name\":\"notify\",\"type\":\"Number\",\"values\":\"1 or 0\"}]},\"getNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\"},\"setNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\/set\",\"params\":[{\"name\":\"url\",\"type\":\"String\",\"values\":\"http:\/\/google.com\"}]},\"reset\":\"\/reset\",\"info\":\"\/\"}}}";

struct Settings {
   int relay;
   int relay_runtime;
   int led;
   long lastupdate;
   long relay_start;
   long relay_stop;
   int reset = 0;
   int notify = 1;
   int endpoint_length;
   char endpoint[80] = "";
};

Settings conf = {};

WiFiManager wifiManager;
std::unique_ptr<ESP8266WebServer> server;

void handleRoot() {
  server->send(200, "application/json", capailities);
}


void handleReset() 
{
  conf.reset = 1;
  writeSettings();
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
  if(conf.relay == 0)
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


void readSwitch()
{
  if(conf.relay == 0)
  {
    switch1state="STATE=OFF";
  }
  else
  {
    switch1state="STATE=ON";
  }

  server->send(200, "text/plain", switch1state);
}


void toggleSwitch()
{
  checkAndRespondToRelayConditionSafeGuard();

  if(conf.relay == 0)
  {
    //relayOn();
    switchOnCompositeRelay();
    switch1state="STATE=ON";
  }
  else
  {
    //relayOff();
    switchOffCompositeRelay();
    switch1state="STATE=OFF";
  }

  server->send(200, "text/plain", switch1state);
}




void switchAOn()
{
  checkAndRespondToRelayConditionSafeGuard();
  
  //relayOn();
  switchOnCompositeRelay();
    
  server->send(200, "text/plain", "STATE=ON");
}




void switchAOff()
{
  checkAndRespondToRelayConditionSafeGuard();
  
  //relayOff();
  switchOffCompositeRelay();

  server->send(200, "text/plain", "STATE=OFF");
}


void getNotify()
{
  readSettings();

  int notify = conf.notify;
  server->send(200, "text/plain", "NOTIFY=" + String(notify));
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
      writeSettings();
      server->send(200, "text/plain", "notify=" + String(conf.notify));      
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
  //Serial.println(String(liquidLevelSensorReadIn) + "LIQUID_LEVEL_OK = " + String(LIQUID_LEVEL_OK));
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

  if(conf.relay == 1)
  {
    max_runtime = conf.relay_runtime * 1000;
    timeover = ((millis() - conf.relay_start) > max_runtime);
    
    if(!LIQUID_LEVEL_OK || timeover)
    {
      //relayOff();
      switchOffCompositeRelay();
    }
  }
}


void switchOffCompositeRelay()
{
  if(RELAY_ON){    
    conf.relay=0;
    conf.relay_stop = millis();
    digitalWrite(RELAY1, LOW);
    digitalWrite(RELAY2, HIGH);
    RELAY_ON = false;
  }
}


void switchOnCompositeRelay()
{
  if(!RELAY_ON){
    conf.relay=1;
    conf.relay_start = millis();    
    digitalWrite(RELAY1, HIGH);
    digitalWrite(RELAY2, LOW);
    RELAY_ON = true;
  }
}


boolean isCompositeRelayOn()
{
  int relay_1_state = digitalRead(RELAY1_READER);
  int relay_2_state = digitalRead(RELAY2_READER);

  if(relay_2_state == 1 && relay_1_state == 0){
    return true;
  }
  else{
    return false;
  }
}


boolean isPumpRunning()
{
  int sensor_state = digitalRead(PUMP_SENSOR);
  if(sensor_state == HIGH){
    return true;
  }else{
    return false;
  }
}


void setup() {

  Serial.begin(9600);

  // start eeprom
  EEPROM.begin(512);

  // Check for reset and do reset routine
  readSettings();
  if(conf.reset == 1){
    debugPrint("Reset flag detected!");    
    doReset();
  }

  pinMode(RELAY1, OUTPUT); 
  digitalWrite(RELAY1, LOW);
  
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY2, HIGH);

  pinMode(RELAY1_READER, INPUT);
  pinMode(RELAY2_READER, INPUT);
  pinMode(LIQUID_LEVEL_SENSOR, INPUT);

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);  

  pinMode(PUMP_SENSOR, INPUT);


  char APNAME[NAME.length() + 1];
  NAME.toCharArray(APNAME, NAME.length() + 1);
  wifiManager.autoConnect(APNAME, "iot@123!");

  //if you get here you have connected to the WiFi
  debugPrint("connected...yeey :)");
  
  server.reset(new ESP8266WebServer(WiFi.localIP(), 80));

  server->on("/", handleRoot);
  server->on("/reset", handleReset);

  server->on("/switch/1", readSwitch);
  server->on("/switch/1/set", toggleSwitch);
  server->on("/switch/1/set/on", switchAOn);
  server->on("/switch/1/set/off", switchAOff);
  server->on("/switch/1/runtime", getSwitchRuntime);
  server->on("/switch/1/runtime/set", setSwitchRuntime);
  server->on("/switch/all", readAllSwitches);
  server->on("/notify", getNotify);
  server->on("/notify/set", setNotify);
  server->on("/notify/url", getNotifyURL);
  server->on("/notify/url/set", setNotifyURL);
  
  server->onNotFound(handleNotFound);
  server->begin();
  
  debugPrint("HTTP server started");
  debugPrint(String(WiFi.localIP()));
}

void loop() 
{
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
      relayConditionSafeGuard();
      delay(3);

      if(millis() - system_start_time > wait_time)
      {
        server->handleClient();
      }
    }
}



void doReset()
{
  conf.relay = 0;
  conf.relay_runtime = 60;
  conf.led = 1;
  conf.lastupdate = 0;
  conf.relay_start = 0;
  conf.relay_stop = 0;
  conf.endpoint_length = 0;
  conf.reset = 0;
  conf.notify = 0;
  memset(conf.endpoint, 0, sizeof(conf.endpoint));
  
  eraseSettings();   
  delay(1000);
  wifiManager.resetSettings();
  delay(1000);
}



void initSettings()
{
  readSettings();

  if(conf.relay_runtime <= 0)
  {
    debugPrint("Setting defaults");
    
    String url = "0.0.0.0";
    char tmp[url.length() + 1];
    url.toCharArray(tmp, url.length() + 1);

    conf.endpoint_length = url.length();
    memset(conf.endpoint, 0, sizeof(conf.endpoint));
    strncpy(conf.endpoint, tmp, strlen(tmp));
    
    conf.notify = 0;
    conf.relay_runtime = DEFAULT_RUNTIME;
  }  

  system_start_time = millis();
  conf.relay=0;
  conf.led=1;
  conf.lastupdate = 0;
  conf.relay_start = 0;
  conf.relay_stop = 0;

  // start state
  LIQUID_LEVEL_OK = true;

  writeSettings();
}




void eraseSettings()
{
  debugPrint("Erasing eeprom...");
  
  for (int i = 0; i < 512; i++){
    EEPROM.write(i, 0);
  }
}



void writeSettings() 
{
  eeAddress = 0;
  conf.lastupdate = millis();

  EEPROM.write(eeAddress, conf.relay);
  eeAddress++;  
  EEPROM.write(eeAddress, conf.relay_runtime);
  eeAddress++;
  EEPROM.write(eeAddress, conf.led);
  eeAddress++;
  EEPROM.write(eeAddress, conf.lastupdate);
  eeAddress++;
  EEPROM.write(eeAddress, conf.relay_start);
  eeAddress++;
  EEPROM.write(eeAddress, conf.relay_stop);
  eeAddress++;
  EEPROM.write(eeAddress, conf.reset);
  eeAddress++;
  EEPROM.write(eeAddress, conf.notify);
  eeAddress++;
  EEPROM.write(eeAddress, conf.endpoint_length);

  eeAddress++;
  writeEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  EEPROM.commit();
  
  debugPrint("Conf saved");
  debugPrint(String(conf.relay));
  debugPrint(String(conf.relay_runtime));
  debugPrint(String(conf.led));
  debugPrint(String(conf.lastupdate));
  debugPrint(String(conf.relay_start));
  debugPrint(String(conf.relay_stop));
  debugPrint(String(conf.reset));
  debugPrint(String(conf.notify));
  debugPrint(String(conf.endpoint_length));
  debugPrint(String(conf.endpoint));;
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
  
  conf.relay = EEPROM.read(eeAddress);
  eeAddress++;
  conf.relay_runtime = EEPROM.read(eeAddress);
  eeAddress++;
  conf.led = EEPROM.read(eeAddress);
  eeAddress++;
  conf.lastupdate = EEPROM.read(eeAddress);
  eeAddress++;
  conf.relay_start = EEPROM.read(eeAddress);
  eeAddress++;
  conf.relay_stop = EEPROM.read(eeAddress);
  eeAddress++;
  conf.reset = EEPROM.read(eeAddress);
  eeAddress++;
  conf.notify = EEPROM.read(eeAddress);
  eeAddress++;
  conf.endpoint_length = EEPROM.read(eeAddress);

  eeAddress++;
  readEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  debugPrint("Conf read");
  debugPrint(String(conf.relay));
  debugPrint(String(conf.relay_runtime));
  debugPrint(String(conf.led));
  debugPrint(String(conf.lastupdate));
  debugPrint(String(conf.relay_start));
  debugPrint(String(conf.relay_stop));
  debugPrint(String(conf.reset));
  debugPrint(String(conf.notify));
  debugPrint(String(conf.endpoint_length));
  debugPrint(String(conf.endpoint));
}



void readEEPROM(int startAdr, int maxLength, char* dest) {

  for (int i = 0; i < maxLength; i++) {
    dest[i] = char(EEPROM.read(startAdr + i));
  }
}


void debugPrint(String message){
  if(debug){
    Serial.println(message);
  }
}


