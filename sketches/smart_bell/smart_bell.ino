#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <RCSwitch.h>
#include <math.h>
#include <ArduinoLog.h>

#define ADC A0
#define RF_TRANSMIT 4
#define BELL_SENSOR_RELAY 12
#define DETECT_LED 5
#define NETWORK_LED 14

IPAddress local_ip(192,168,5,1);
IPAddress gateway(192,168,5,1);
IPAddress subnet(255,255,255,0);

String ID = "";
String capabilities = "{\"name\":\"%s\",\"devices\":{\"name\":\"Bell Sensor\",\"actions\":{\"getTimeout\":{\"method\":\"get\",\"path\":\"\/sensor\/1\/timeout\"},\"setTimeout\":{\"path\":\"\/sensor\/1\/timeout\/set\",\"params\":[{\"name\":\"time\",\"type\":\"Number\",\"values\":\"Between 15 and 60\"}]}}},\"global\":{\"actions\":{\"getNotify\":{\"method\":\"get\",\"path\":\"\/notify\"},\"setNotify\":{\"path\":\"\/notify\/set\",\"params\":[{\"name\":\"notify\",\"type\":\"Number\",\"values\":\"1 or 0\"}]},\"getNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\"},\"setNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\/set\",\"params\":[{\"name\":\"url\",\"type\":\"String\",\"values\":\"http:\/\/google.com\"}]},\"getBellNotifyMode\":{\"method\":\"get\",\"path\":\"\/notify\/mode\"},\"setBellNotifyMode\":{\"method\":\"get\",\"path\":\"\/notify\/mode\/set\",\"params\":[{\"name\":\"mode\",\"type\":\"Number\",\"values\":\"1|2|3\",\"comment\":\"1 implies RF only | 2 implies Wifi only | 3 imples RF and Wifi transmission\"}]},\"reset\":\"\/reset\",\"info\":\"\/\"}}}";

const char TYPE_CODE[10] = "SM-BL"; // Code for Smart Bell
const char UPDATE_SKETCH[7] = "sketch";
const char UPDATE_SPIFFS[7] = "spiffs";
const char UPDATE_ENDPOINT[40] = "https://iot.flashvisions.com/update";
const char INIT_ENDPOINT[40] = "https://iot.flashvisions.com/initialize";
const char AP_DEFAULT_PASS[10] = "iot@123!";
const float BELL_INPUT_VOLTAGE_THRESHOLD = 2.0;
const int RFCODE = 73964;
const int EEPROM_LIMIT = 512;
const int BELL_INPUT_THRESHOLD = 900;

// Debugging mode
boolean debug = true;
int eeAddress = 0;
int bell_input;
float bell_input_voltage;
boolean BELL_DETECTION_LOCK = false;
boolean BELL_SENSOR_ON = false;
boolean BELL_ON = false;
boolean BELL_TIMEOUT_BREACHED = false;
long BELL_TIMEOUT = 35000;
boolean canNotify = false;
long lastDetection;
long sinceLastDetection;
boolean LED_ON;
boolean NET_LED_ON;
String IP;
boolean inited;

WiFiManager wifiManager;
RCSwitch mySwitch = RCSwitch();

struct Settings {
  int notify = 1;
  long timestamp;
  int starting = 0;
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



String generateClientID()
{
  byte mac[6]; 
  WiFi.macAddress(mac);
  String chipId = String(ESP.getChipId(), HEX);
  String flashChipId = String(ESP.getFlashChipId(), HEX);

  return String(TYPE_CODE) + "-" + String(mac[0], HEX) + chipId + String(mac[1], HEX) + "-" + String(mac[2], HEX) + flashChipId + String(mac[3], HEX) + "-" + String(mac[4], HEX) + String(mac[5], HEX);
}


String generateAPName()
{
  char ran[8]; 
  gen_random(ran, 8);
  
  return String(TYPE_CODE) + "-" + String(ran);
}


void gen_random(char *s, const int len) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    for (int i = 0; i < len; ++i) {
        s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    s[len] = 0;
}

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
  Serial.begin(9600);

  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  Log.begin(LOG_LEVEL_NOTICE, &Serial);
  
  // start eeprom
  EEPROM.begin(EEPROM_LIMIT);

  // Get ID
  ID = generateClientID();

  // Check for reset and do reset routine
  readSettings();

  preStartUp();
  
  if(conf.reset == 1){
    Log.notice("Reset flag detected!" CR);    
    doReset();
  }

  // init led pin
  pinMode(DETECT_LED, OUTPUT);
  pinMode(NETWORK_LED, OUTPUT);

  // bell sensor -> Isolated Mode Power Supply Controlled through Relay
  pinMode(BELL_SENSOR_RELAY, OUTPUT);
  //enableBellSensor();

  netledOn();

  String NAME = generateAPName();
  char APNAME[NAME.length() + 1];
  NAME.toCharArray(APNAME, NAME.length() + 1);

  wifiManager.setAPStaticIPConfig(local_ip, gateway, subnet);
  wifiManager.autoConnect(APNAME, AP_DEFAULT_PASS);

  //if you get here you have connected to the WiFi
  Log.notice("connected...yeey :" CR);
  netledOff();

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

  Log.notice("HTTP server started" CR);

  IP = String(WiFi.localIP());
  Log.notice("My IP address: : %d.%d.%d.%d" CR, WiFi.localIP()[0],  WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
  
  // Transmitter is connected to Arduino Pin #D2 (04)
  mySwitch.enableTransmit(RF_TRANSMIT);
  mySwitch.setRepeatTransmit(6);
}

void loop() {

  if(!inited){
      inited = true;
      initSettings();
      
      // wait a little
      delay(3000);
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


void preStartUp()
{
  delay(1000);
  
  conf.starting += 1;
  Log.notice("starting count %d" CR, conf.starting);
  
  if(conf.starting >= 3)
  {
    // Check if we have restarted 3 times recently
    conf.reset = 1;
  }
  else
  {
    // less than 3 then just save and continue
    writeSettings();
  }
}


void doReset()
{
  netledOn();
  ledOn();

  delay(3000);
  
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

  netledOff();
  ledOff();
}


void disableBellSensor()
{
  Log.notice("Disabling bell sensor" CR);

  if (BELL_SENSOR_ON) {
    digitalWrite(BELL_SENSOR_RELAY, LOW);
    BELL_SENSOR_ON = false;
  }
}



void enableBellSensor()
{
  Log.notice("Enabling bell sensor" CR);

  if (!BELL_SENSOR_ON) {
    digitalWrite(BELL_SENSOR_RELAY, HIGH);
    BELL_SENSOR_ON = true;
  }
}



void ledOn()
{
  if(!LED_ON){
    digitalWrite(DETECT_LED, HIGH); 
    LED_ON = true;
  }
}


void ledOff()
{
  if(LED_ON){
    digitalWrite(DETECT_LED, LOW); 
    LED_ON = false;
  }
}

void netledOn()
{
  if(!NET_LED_ON){
    digitalWrite(NETWORK_LED, HIGH); 
    NET_LED_ON = true;
  }
}


void netledOff()
{
  if(NET_LED_ON){
    digitalWrite(NETWORK_LED, LOW); 
    NET_LED_ON = false;
  }
}

void checkBell()
{
  bell_input = analogRead(ADC);
  bell_input_voltage = getValueAsVoltage(bell_input);
  //bell_input = 910;
  //debugPrint("bell_input " + String(bell_input));

  // Check alarm state
  sinceLastDetection = millis() - lastDetection;
  
  //BELL_ON = (bell_input > BELL_INPUT_THRESHOLD);
  BELL_ON = (bell_input_voltage > BELL_INPUT_VOLTAGE_THRESHOLD);
  BELL_TIMEOUT_BREACHED = (sinceLastDetection > BELL_TIMEOUT);
  canNotify = (BELL_TIMEOUT_BREACHED && BELL_ON);


  // if 'bell lock timeout' occurred then open bell detection lock
  if (BELL_TIMEOUT_BREACHED && BELL_DETECTION_LOCK) {
    Log.notice("Removing bell detection lock" CR);
    BELL_DETECTION_LOCK = false;
    enableBellSensor();
    ledOff();
  }


  // if 'canNotify' and 'bell detection lock' is open do detection action now
  if (canNotify && !BELL_DETECTION_LOCK)
  {
    Log.notice("Bell on event detected!" CR);
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


float getValueAsVoltage(int val)
{
  float voltage = val * (3.3 / 1024.0);
  voltage = round( voltage * 10.0 ) / 10.0;
  return voltage;
}


void testNotify()
{
  Log.notice("Testing notification!" CR);
  
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
}


void notifyRF()
{
  Log.notice("Sending transmission!" CR);
  mySwitch.send(RFCODE, 24);
}


void notifyURL()
{
  if (!posting)
  {
    readSettings();

    posting = true;
    Log.notice("Sending url call!" CR);

    http.begin(String(conf.endpoint));
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    int httpCode = http.POST("bell=" + String(BELL_DETECTION_LOCK));
    String payload = http.getString();

    Log.trace("httpCode: : %d" CR, httpCode);
    //Log.trace("payload %s:" CR, payload);

    http.end();

    posting = false;
  }
}


void initSettings()
{
  readSettings();
  
  if (conf.notify_mode<1 || conf.notify_mode>3) 
  {
    Log.trace("Setting defaults" CR);
    
    String url = "0.0.0.0";
    char tmp[url.length() + 1];
    url.toCharArray(tmp, url.length() + 1);

    conf.endpoint_length = url.length();
    memset(conf.endpoint, 0, sizeof(conf.endpoint));
    strncpy(conf.endpoint, tmp, strlen(tmp));
    
    conf.notify = 0;
    conf.notify_mode = 3;
    conf.bell_timeout = 35;
    conf.starting = 0;
    
    //writeSettings();
  }

  // reset starting flag
  conf.starting = 0;

  // update bell sensor timeout
  updateBellSensorTimeout();

  // Start with locked sensor to allow callibration
  lastDetection = millis() ;
  disableBellSensor();
  BELL_DETECTION_LOCK = true;

  // save settings
  writeSettings();

  // Detection Led on
  ledOn();
}


// Update the miliseconds of bell sensor timeout
void updateBellSensorTimeout()
{
  if(conf.bell_timeout >= 15){
    BELL_TIMEOUT = conf.bell_timeout * 1000;
  }

  Log.notice("BELL_TIMEOUT : %d" CR, BELL_TIMEOUT);
}


void writeSettings()
{
  eeAddress = 0;
  conf.timestamp = millis();

  EEPROM.write(eeAddress, conf.notify);
  eeAddress++;
  EEPROM.write(eeAddress, conf.timestamp);
  eeAddress++;
  EEPROM.write(eeAddress, conf.starting);
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

  Log.notice("Conf saved" CR);
  //debugPrint(String(conf.notify));
  //debugPrint(String(conf.endpoint_length));
  //debugPrint(String(conf.notify_mode));
  //debugPrint(String(conf.bell_timeout));
  //debugPrint(String(conf.endpoint));
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
  conf.starting = EEPROM.read(eeAddress);
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

  Log.notice("Conf read" CR);
  //debugPrint(String(conf.notify));
  //debugPrint(String(conf.endpoint_length));
  //debugPrint(String(conf.notify_mode));
  //debugPrint(String(conf.bell_timeout));
  //debugPrint(String(conf.endpoint));
}



void readEEPROM(int startAdr, int maxLength, char* dest) {

  for (int i = 0; i < maxLength; i++) {
    dest[i] = char(EEPROM.read(startAdr + i));
  }
}



void eraseSettings()
{
  Log.notice("Erasing eeprom" CR);
  
  for (int i = 0; i < EEPROM_LIMIT; i++)
    EEPROM.write(i, 0);
}
