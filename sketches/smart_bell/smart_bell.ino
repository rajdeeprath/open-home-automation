#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <RCSwitch.h>

#define ADC A0
#define RF_TRANSMIT 4
#define BELL_SENSOR_RELAY 12
#define LED 5

const String NAME = "HMU-BL-002";
String capabilities = "{\"name\":\"" + NAME + "\",\"devices\":{\"name\":\"Bell Sensor\",\"actions\":{\"getTimeout\":{\"method\":\"get\",\"path\":\"\/sensor\/1\/timeout\"},\"setTimeout\":{\"path\":\"\/sensor\/1\/timeout\/set\",\"params\":[{\"name\":\"time\",\"type\":\"Number\",\"values\":\"Between 15 and 60\"}]}}},\"global\":{\"actions\":{\"getNotify\":{\"method\":\"get\",\"path\":\"\/notify\"},\"setNotify\":{\"path\":\"\/notify\/set\",\"params\":[{\"name\":\"notify\",\"type\":\"Number\",\"values\":\"1 or 0\"}]},\"getNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\"},\"setNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\/set\",\"params\":[{\"name\":\"url\",\"type\":\"String\",\"values\":\"http:\/\/google.com\"}]},\"getBellNotifyMode\":{\"method\":\"get\",\"path\":\"\/notify\/mode\"},\"setBellNotifyMode\":{\"method\":\"get\",\"path\":\"\/notify\/mode\/set\",\"params\":[{\"name\":\"mode\",\"type\":\"Number\",\"values\":\"1|2|3\",\"comment\":\"1 implies RF only | 2 implies Wifi only | 3 imples RF and Wifi transmission\"}]},\"reset\":\"\/reset\",\"info\":\"\/\"}}}";

// Debugging mode
boolean debug = true;
int RFCODE = 73964;

int eeAddress = 0;
int bell_input;
boolean BELL_DETECTION_LOCK = false;
boolean BELL_SENSOR_ON = false;
boolean BELL_ON = false;
boolean BELL_TIMEOUT_BREACHED = false;
int BELL_INPUT_THRESHOLD = 900;
long BELL_TIMEOUT = 35000;
boolean canNotify = false;
long lastDetection;
long sinceLastDetection;
boolean LED_ON;
String IP;
boolean inited;

WiFiManager wifiManager;
RCSwitch mySwitch = RCSwitch();

struct Settings {
  int notify = 1;
  long timestamp;
  int endpoint_length;
  int notify_mode;
  int reset = 0;
  int bell_timeout = 35;
  char endpoint[50] = "";
};


Settings conf = {};
std::unique_ptr<ESP8266WebServer> server;


HTTPClient http;
boolean posting;

/**************************************************/

void handleRoot() {
  server->send(200, "application/json", capabilities);
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




void getBellNotifyMode()
{
  int notify_mode;

  readSettings();

  notify_mode = conf.notify_mode;

  if (notify_mode < 1 || notify_mode > 3)
  {
    server->send(400, "text/plain", "Invalid notify mode");
  }
  else
  {
    server->send(200, "text/plain", "NOTIFYMODE=" + String(notify_mode));
  }
}



void setBellNotifyMode()
{
  int notify_mode;

  if (server->hasArg("mode"))
  {
    notify_mode = server->arg("mode").toInt();;

    if (notify_mode < 1 || notify_mode > 3)
    {
      server->send(400, "text/plain", "Invalid notify mode");
    }
    else
    {
      conf.notify_mode = notify_mode;
      writeSettings();

      server->send(200, "text/plain", "NOTIFYMODE=" + String(notify_mode));      
    }
  }
  else
  {
    server->send(400, "text/plain", "No value provided");
  }
}




void getBellSensorTimeout()
{
  int timeout;

  readSettings();

  timeout = conf.bell_timeout;

  if (timeout<15)
  {
    server->send(400, "text/plain", "Invalid sensor timeout value! Minimum should be 15");
  }
  else
  {
    server->send(200, "text/plain", "TIMEOUT=" + String(timeout));
  }
}



void setBellSensorTimeout()
{
  int timeout;

  if (server->hasArg("time"))
  {
    timeout = server->arg("time").toInt();;

    if (timeout < 15)
    {
      server->send(400, "text/plain", "Invalid sensor timeout value! Minimum should be 15");
    }
    else
    {
      conf.bell_timeout = timeout;
      updateBellSensorTimeout();      

      writeSettings();
      server->send(200, "text/plain", "TIMEOUT=" + String(timeout));      
    }
  }
  else
  {
    server->send(400, "text/plain", "No timeout value provided");
  }
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
    server->send(200, "text/plain", "URL=" + url);
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

      writeSettings();
      
      server->send(200, "text/plain", "url=" + url);
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


void setup()
{
  Serial.begin(57600);
  
  // start eeprom
  EEPROM.begin(512);

  // Check for reset and do reset routine
  readSettings();
  if(conf.reset == 1){
    debugPrint("Reset flag detected!");    
    doReset();
  }

  // init led pin
  pinMode(LED, OUTPUT);

  // bell sensor -> Isolated Mode Power Supply Controlled through Relay
  pinMode(BELL_SENSOR_RELAY, OUTPUT);
  //enableBellSensor();

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
  server->on("/notify/test", testNotify);

  server->on("/notify/url", getBellNotifyURL);
  server->on("/notify/url/set", setBellNotifyURL);

  server->on("/notify/mode", getBellNotifyMode);
  server->on("/notify/mode/set", setBellNotifyMode);

  server->on("/sensor/1/timeout", getBellSensorTimeout);
  server->on("/sensor/1/timeout/set", setBellSensorTimeout);

  server->onNotFound(handleNotFound);
  server->begin();

  debugPrint("HTTP server started");

  IP = String(WiFi.localIP());
  debugPrint(String(WiFi.localIP()));

  // Transmitter is connected to Arduino Pin #D2 (04)
  mySwitch.enableTransmit(RF_TRANSMIT);
  mySwitch.setRepeatTransmit(6);
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
    checkBell();

    delay(3);
    server->handleClient();
  }
}


void doReset()
{
  conf.notify = 0;
  conf.timestamp = 0;
  conf.endpoint_length = 0;
  conf.notify_mode = 0;
  conf.reset = 0;
  memset(conf.endpoint, 0, sizeof(conf.endpoint));
  
  eraseSettings();   
  delay(1000);
  wifiManager.resetSettings();
  delay(1000);
}


void disableBellSensor()
{
  debugPrint("Disabling bell sensor");

  if (BELL_SENSOR_ON) {
    digitalWrite(BELL_SENSOR_RELAY, LOW);
    BELL_SENSOR_ON = false;
  }
}



void enableBellSensor()
{
  debugPrint("Enabling bell sensor");

  if (!BELL_SENSOR_ON) {
    digitalWrite(BELL_SENSOR_RELAY, HIGH);
    BELL_SENSOR_ON = true;
  }
}



void ledOn()
{
  if(!LED_ON){
    digitalWrite(LED, HIGH); 
    LED_ON = true;
  }
}


void ledOff()
{
  if(LED_ON){
    digitalWrite(LED, LOW); 
    LED_ON = false;
  }
}


void checkBell()
{
  bell_input = analogRead(ADC);
  //bell_input = 910;
  //debugPrint("bell_input " + String(bell_input));

  // Check alarm state
  sinceLastDetection = millis() - lastDetection;
  BELL_ON = (bell_input > BELL_INPUT_THRESHOLD);
  BELL_TIMEOUT_BREACHED = (sinceLastDetection > BELL_TIMEOUT);
  canNotify = (BELL_TIMEOUT_BREACHED && BELL_ON);


  // if 'bell lock timeout' occurred then open bell detection lock
  if (BELL_TIMEOUT_BREACHED && BELL_DETECTION_LOCK) {
    debugPrint("Removing bell detection lock");
    BELL_DETECTION_LOCK = false;
    enableBellSensor();
    ledOff();
  }


  // if 'canNotify' and 'bell detection lock' is open do detection action now
  if (canNotify && !BELL_DETECTION_LOCK)
  {
    debugPrint("Bell on event detected!");
    lastDetection = millis();

    // protect bell sensor from maniacs! => TURN ON BELL DETECTION LOCK
    disableBellSensor();
    BELL_DETECTION_LOCK = true;
    ledOn();

    if (conf.notify == 1)
    {

      // if RF notify is allowed then do it
      if(conf.notify_mode == 1 || conf.notify_mode == 3){
        // VIA RF
        notifyRF();
      }


      // if URL notify is allowed tdo it
      if(conf.notify_mode == 2 || conf.notify_mode == 3){
        // VIA URL
        if (conf.endpoint_length > 4 && conf.endpoint != "0.0.0.0") {
          notifyURL();
        }
      }
      
    }
  }
}


void testNotify()
{
  debugPrint("Testing notification");
  
  if (canNotify)
  {
    if (conf.notify == 1)
    {
      // if RF notify is allowed then do it
      if(conf.notify_mode == 1 || conf.notify_mode == 3){
        notifyRF();
      }

      // if URL notify is allowed tdo it
      if(conf.notify_mode == 2 || conf.notify_mode == 3){
        if (conf.endpoint_length > 4 && conf.endpoint != "0.0.0.0") {
          notifyURL();
        }
      }
  }
}


void notifyRF()
{
  debugPrint("Sending transmission");
  mySwitch.send(RFCODE, 24);
}


void notifyURL()
{
  if (!posting)
  {
    readSettings();

    posting = true;
    debugPrint("Sending url call");

    http.begin(String(conf.endpoint));
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpCode = http.POST("bell=" + String(BELL_DETECTION_LOCK));

    String payload = http.getString();

    debugPrint(String(httpCode));
    debugPrint(String(payload));

    http.end();

    posting = false;
  }
}


void initSettings()
{
  readSettings();
  
  if (conf.notify_mode<1 || conf.notify_mode>3) 
  {
    debugPrint("Setting defaults");
    
    String url = "0.0.0.0";
    char tmp[url.length() + 1];
    url.toCharArray(tmp, url.length() + 1);

    conf.endpoint_length = url.length();
    memset(conf.endpoint, 0, sizeof(conf.endpoint));
    strncpy(conf.endpoint, tmp, strlen(tmp));
    
    conf.notify = 0;
    conf.notify_mode = 3;
    conf.bell_timeout = 35;
    
    writeSettings();
  }


  // update bell sensor timeout
  updateBellSensorTimeout();

  // Start with locked sensor to allow callibration
  lastDetection = millis() ;
  disableBellSensor();
  BELL_DETECTION_LOCK = true;
  ledOn();
}


// Update the miliseconds of bell sensor timeout
void updateBellSensorTimeout()
{
  if(conf.bell_timeout >= 15){
    BELL_TIMEOUT = conf.bell_timeout * 1000;
  }

  debugPrint("BELL_TIMEOUT : " + String(BELL_TIMEOUT));
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
  EEPROM.write(eeAddress, conf.notify_mode);
  eeAddress++;
  EEPROM.write(eeAddress, conf.bell_timeout);  
  eeAddress++;
  EEPROM.write(eeAddress, conf.reset);  

  eeAddress++;
  writeEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  EEPROM.commit();

  debugPrint("Conf saved");
  debugPrint(String(conf.notify));
  debugPrint(String(conf.endpoint_length));
  debugPrint(String(conf.notify_mode));
  debugPrint(String(conf.bell_timeout));
  debugPrint(String(conf.endpoint));
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
  conf.notify_mode = EEPROM.read(eeAddress);
  eeAddress++;
  conf.bell_timeout = EEPROM.read(eeAddress);  
  eeAddress++;
  conf.reset = EEPROM.read(eeAddress);

  eeAddress++;
  readEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  debugPrint("Conf read");
  debugPrint(String(conf.notify));
  debugPrint(String(conf.endpoint_length));
  debugPrint(String(conf.notify_mode));
  debugPrint(String(conf.bell_timeout));
  debugPrint(String(conf.endpoint));
}



void readEEPROM(int startAdr, int maxLength, char* dest) {

  for (int i = 0; i < maxLength; i++) {
    dest[i] = char(EEPROM.read(startAdr + i));
  }
}



void eraseSettings()
{
  debugPrint("Erasing eeprom...");
  
  for (int i = 0; i < 512; i++)
    EEPROM.write(i, 0);
}




void debugPrint(String message) {
  if (debug) {
    Serial.println(message);
  }
}
