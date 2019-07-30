#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
// #include <DNSServer.h>
// #include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <math.h>
#include <ArduinoLog.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Arduino.h>  // for type definitions
#include <MQTT.h>


IPAddress local_ip(192, 168, 5, 1);
IPAddress gateway(192, 168, 5, 1);
IPAddress subnet(255, 255, 255, 0);

#define SKETCH_VERSION "001"
#define SPIFFS_VERSION "001"

String ID;
const char TYPE_CODE[10] = "SM-BL"; // Code for Smart Bell
const char UPDATE_SKETCH[7] = "sketch";
const char UPDATE_SPIFFS[7] = "spiffs";
char* UPDATE_ENDPOINT = "https://iot.flashvisions.com/api/public/update";
char* INIT_ENDPOINT = "https://iot.flashvisions.com/api/public/initialize";
const char AP_DEFAULT_PASS[10] = "iot@123!";
const int EEPROM_LIMIT = 512;
const char fingerprint[80] = "19:4E:21:11:C1:69:2D:4E:0A:6B:F2:51:85:44:03:0A:10:2A:AE:BF";// iot.flashvisions.com
const long utcOffsetInSeconds = 19800; // INDIA

int eeAddress = 0;
String IP;
boolean inited = false;

WiFiClient net;
WiFiManager wifiManager;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);


struct Settings {
  int valid = 1;
  int mqtt_port = 0;
  int mqtt_server_length = 0;
  char mqtt_server[50] = "0.0.0.0";
  long timestamp = 0;
  int reset = 0;
};


struct Message {
  char msg[200];
  int requires_ack = 1;
  int ack = 0;
  long timestamp = 0;
};
struct Message messages[5];

Settings conf = {};
std::unique_ptr<ESP8266WebServer> server;

HTTPClient http;
WiFiClient espClient;
MQTTClient client;


template <class T> int EEPROM_writeAnything(int ee, const T& value)
{
    const byte* p = (const byte*)(const void*)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++)
          EEPROM.write(ee++, *p++);
    return i;
}

template <class T> int EEPROM_readAnything(int ee, T& value)
{
    byte* p = (byte*)(void*)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++)
          *p++ = EEPROM.read(ee++);
    return i;
}


void publish_message(char *payload, int requires_ack)
{
  Message record;

  //record.msg = payload;
  record.requires_ack = requires_ack;
  record.ack = 0;
  record.timestamp = timeClient.getEpochTime();

  // store record in array
  // generate topic
  
  //client.publish("outTopic", "hello world");

}


void loadConfiguration()
{
  Log.notice("loadConfiguration" CR);

  HTTPClient https;
  boolean failed = true;

  String url = String(INIT_ENDPOINT) + "?id=" + String(ID) + "&ver=" + String(SKETCH_VERSION);

  http.begin(url, fingerprint);
  int httpCode = http.GET();

  if (httpCode == 200)
  {
    String payload = http.getString();
    Serial.println(payload);
    char json[payload.length() + 1];
    payload.toCharArray(json, payload.length() + 1);

    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);

    if (root.success())
    {
      int code = root["code"];
      if (code == 200)
      {
        String mqtt_host = root["data"]["mqtt_host"];
        conf.mqtt_server[mqtt_host.length() + 1];
        mqtt_host.toCharArray(conf.mqtt_server, mqtt_host.length() + 1);

        conf.mqtt_port = root["data"]["mqtt_port"];
        conf.timestamp = root["data"]["timestamp"];

        conf.mqtt_server_length = sizeof(conf.mqtt_server);
        conf.valid = 1;

        failed = false;
      }
    }
  }

  http.end();

  if (!failed)
  {
    writeSettings();    
    inited = true;
  }
  else
  {
    conf.valid = 0;
    Log.notice("Failed to load config. Try again after sometime" CR);
  }
}


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


void setUpMqTTClient()
{
  client.begin(conf.mqtt_server, conf.mqtt_port, net);
  client.onMessage(messageReceived);
}


void messageReceived(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);
}

void connect() {

  Log.notice("Connecting..." CR);

  char CLIENTID[ID.length() + 1];
  ID.toCharArray(CLIENTID, ID.length() + 1);
  
  while (!client.connect(CLIENTID, "anonymous", "anonymous")) {
    Log.notice("." CR);
    delay(1000);
  }

  Log.notice("Connected..." CR);

  //client.subscribe("/hello");
  // client.unsubscribe("/hello");
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

  // Check for reset and do reset routine
  readSettings();

  if (conf.reset == 1) {
    Log.notice("Reset flag detected!" CR);
    doReset();
  }

  String NAME = generateAPName();
  char APNAME[NAME.length() + 1];
  NAME.toCharArray(APNAME, NAME.length() + 1);

  wifiManager.setAPStaticIPConfig(local_ip, gateway, subnet);
  wifiManager.autoConnect(APNAME, AP_DEFAULT_PASS);

  //if you get here you have connected to the WiFi
  Log.notice("connected...yeey :" CR);

  IP = String(WiFi.localIP());
  Log.notice("My IP address: : %d.%d.%d.%d" CR, WiFi.localIP()[0],  WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);

  ID = generateClientID();
}

void loop() {

  if (!inited)
  {
    initSettings();
  }
  else
  {
    timeClient.update();
    
    if (!client.connected()) 
    {
      connect();
    }
    else
    {
      client.loop();
      delay(10);
    }
  }

  if (conf.reset == 1)
  {
    delay(5000);
    ESP.restart();
  }
}


void doReset()
{
  conf.timestamp = 0;
  conf.mqtt_server_length = 0;
  conf.reset = 0;
  memset(conf.mqtt_server, 0, sizeof(conf.mqtt_server));

  eraseSettings();
  delay(1000);
  wifiManager.resetSettings();
  delay(1000);
}


void initSettings()
{
  // Get ID
  ID = generateClientID();

  readSettings();

  if (conf.valid == 1)
  {
    inited = true;
    timeClient.begin();
    setUpMqTTClient();
  }
  else
  {
    delay(5000);
    Log.notice("RETRYING CONFIGURATION LOAD" CR);
    loadConfiguration();
  }
}



void updateFirmware()
{
  Log.notice("UPDATING" CR);

  boolean sketch = true;
  String msg;
  t_httpUpdate_return ret;

  ESPhttpUpdate.rebootOnUpdate(false);

  Log.notice("UPDATING" CR);
  //ret = ESPhttpUpdate.update("https://iot.flashvisions.com/api/public/update", "", String(fingerprint));
  ret = ESPhttpUpdate.update("http://iot.flashvisions.com/firmware/iot_template.ino.nodemcu.bin");

  if (ret != HTTP_UPDATE_NO_UPDATES)
  {
    if (ret == HTTP_UPDATE_OK)
    {
      Log.notice("UPDATE SUCCEEDED" CR);
      conf.reset = 1;
    }
    else
    {
      if (ret == HTTP_UPDATE_FAILED)
      {
        Log.notice("Upgrade Failed" CR);
        Log.notice("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      }
    }
  }
}


void writeSettings()
{
  Log.notice("Writing conf" CR);  
  eeAddress = 0;
  
  EEPROM.write(eeAddress, conf.valid);
  eeAddress++;

  EEPROM_writeAnything(eeAddress, conf);
  EEPROM.commit();

  Log.notice("Conf saved" CR);
  Log.notice("conf.valid %d" CR, conf.valid);
  Log.notice("conf.mqtt_server %s" CR, conf.mqtt_server);
  Log.notice("conf.timestamp %d" CR, conf.timestamp);
  Log.notice("conf.mqtt_server_length %d" CR, conf.mqtt_server_length);
  Log.notice("conf.mqtt_port %d" CR, conf.mqtt_port);
}



void readSettings()
{
  Log.notice("Reading conf" CR);
  eeAddress = 0;

  conf.valid = EEPROM.read(eeAddress);
  eeAddress++;
  
  if (conf.valid != 1)
  {
      Log.notice("Conf not valid, skip reading" CR);
  }
  else
  {
      EEPROM_readAnything(eeAddress, conf);
     
      Log.notice("Conf read" CR);      
      Log.notice("conf.valid %d" CR, conf.valid);
      Log.notice("conf.mqtt_server %s" CR, conf.mqtt_server);
      Log.notice("conf.timestamp %d" CR, conf.timestamp);
      Log.notice("conf.mqtt_server_length %d" CR, conf.mqtt_server_length);
      Log.notice("conf.mqtt_port %d" CR, conf.mqtt_port);
  }
}


void eraseSettings()
{
  Log.notice("Erasing eeprom" CR);

  for (int i = 0; i < EEPROM_LIMIT; i++)
    EEPROM.write(i, 0);
}
