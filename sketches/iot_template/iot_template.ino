#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
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
#define SOUND_PIN A0

String ID;
const char TYPE_CODE[10] = "SM-BL"; // Code for Smart Bell
const char UPDATE_SKETCH[7] = "sketch";
const char UPDATE_SPIFFS[7] = "spiffs";
char* UPDATE_ENDPOINT = "http://iot.flashvisions.com/api/public/update";
char* INIT_ENDPOINT = "http://iot.flashvisions.com/api/public/initialize";
const char AP_DEFAULT_PASS[10] = "iot@123!";
const int EEPROM_LIMIT = 512;
const long utcOffsetInSeconds = 19800; // INDIA
const int MQTT_MAX_CONNECT_TRIES = 2;
const int MAX_MESSAGES_STORE = 20;
const int MAX_MESSAGE_STORE_TIME_SECONDS = 5;

int eeAddress = 0;
String IP;
boolean inited = false;
int mqtt_connect_try = 0;
boolean mqtt_connected = false;
int message_counter = 0;

String PUB_TOPIC = "";
String SUB_TOPIC = "";

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
  char id[15];
  char topic[60];
  char msg[100];
  int requires_ack = 1;
  int published = 0;
  int publish_error = 0;
  int ack_received = 0;
  long timestamp = 0;
};


struct Message messages[MAX_MESSAGES_STORE];

struct Settings conf = {};

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


void publish_data(const char topic[], const char payload[], int requires_ack)
{
  if(message_counter < MAX_MESSAGES_STORE)
  {
    Message record = {};
    strcpy(record.topic, topic);
    strcpy(record.msg, payload);
    record.requires_ack = requires_ack;
    record.timestamp = timeClient.getEpochTime();
    
    // store record in array and increment counter
    messages[message_counter] = record;
    message_counter = message_counter + 1;
  }
  else
  {
      Log.notice("Message queue full. Cannot store more messages" CR);
  }
}


void processMessages()
{
  Log.notice("Processing messages" CR);
  
  int i;
  int indices[10];
  int gc_counter = 0;
  long curr_timestamp = timeClient.getEpochTime();
  
  Log.notice("Total messages earlier = %d" CR, message_counter);
  
  for(i = message_counter ; i-- > 0;)
  {  
    Log.notice("Index = %d, Current = %d , Record = %d" CR, i, curr_timestamp, messages[i].timestamp);

    if(messages[i].published != 1)
    {
      if(mqtt_connected)
      {
        Log.trace("Publishing message" CR);
        
        if(publishNow(messages[i].topic, messages[i].msg, strlen(messages[i].msg), true, 1))
        {
          messages[i].published = 1;
          Log.notice("Published" CR);  
        }
        else
        {
          messages[i].publish_error = 1;
          Log.notice("Published Failed" CR);
        }
      }
    }
    else if(messages[i].timestamp > 0)
    {
      if((curr_timestamp - messages[i].timestamp > MAX_MESSAGE_STORE_TIME_SECONDS) || messages[i].publish_error == 1 || (messages[i].requires_ack == 1 && messages[i].ack_received != 1))
      {
        int diff = curr_timestamp - messages[i].timestamp;
        Log.notice("%d sec Old message record found at %d. Marking for removal" CR, diff, i);
        indices[gc_counter] = i;
        gc_counter++;
        
        Log.notice("gc_counter %d" CR, gc_counter);
      }
    }
  }

  cleanUpMessages(gc_counter, indices);
  Log.notice("Total messages later = %d" CR, message_counter);
}



void cleanUpMessages(int itemsToClean, int item_indices[])
{
  for(int i=0; i<itemsToClean;i++)
  {
    int pos = item_indices[i];
    
    if(pos>0)
    {
      Log.trace("Processing/Removing element at position %d" CR, pos);
        
      for(i=pos-1; i<message_counter-1;i++){
        messages[i] = messages[i+1];
      }
      message_counter = message_counter - 1; 
    }
    else
    {
      if(message_counter == 1)
      {
        Log.trace("Only one item" CR);
        memset(messages, 0, sizeof(messages));
        message_counter = 0;
      }
      else
      {
        Log.trace("%d items" CR, message_counter);
      }
    }
  }
}


boolean publishNow(const char topic[], const char payload[], int len, bool retained, int qos)
{
  return client.publish(topic, payload, len, retained, qos);
}


void loadConfiguration()
{
  Log.notice("loadConfiguration" CR);
  boolean failed = true;

  String url = String(INIT_ENDPOINT) + "?id=" + String(ID) + "&ver=" + String(SKETCH_VERSION);

  http.begin(url);
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
    mqtt_connected = false;
    mqtt_connect_try++;
    
    if(mqtt_connect_try > MQTT_MAX_CONNECT_TRIES){
      Log.notice("Too many retries. Skipping..." CR);
      mqtt_connect_try = 0;
      return;
    }

    Log.notice("." CR);
    delay(2000);
  }

  Log.notice("Connected..." CR);
  mqtt_connected = true;

  //client.subscribe("/hello");
  // client.unsubscribe("/hello");
}


void setupSensors()
{
  pinMode(SOUND_PIN, INPUT);
}


void readSensor()
{
  int statusSensor = analogRead (SOUND_PIN);
  //Log.notice("VAL = %d" CR, statusSensor);
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

  // init sensors
  setupSensors();

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

  IP = WiFi.localIP().toString();
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
    readSensor();
    
    if (!client.connected()) 
    {
      char payload[] = "{\"msg\":\"hello\"}";
      PUB_TOPIC = "dt/home/" + ID + "/";
      char topic[PUB_TOPIC.length() + 1];
      PUB_TOPIC.toCharArray(topic, PUB_TOPIC.length() + 1);
      publish_data(topic, payload, 1);
      connect();
    }
    else
    {
      client.loop();
    }

    delay(2000);
    processMessages();
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
    //updateFirmware();
  }
  else
  {
    Log.notice("RETRYING CONFIGURATION LOAD" CR);
    delay(5000);
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

  ret = ESPhttpUpdate.update("http://iot.flashvisions.com/firmware/iot_template.ino.nodemcu.bin");

  if (ret != HTTP_UPDATE_NO_UPDATES)
  {
    if (ret == HTTP_UPDATE_OK)
    {
      Log.notice("UPDATE SUCCEEDED" CR);
      ESP.restart();
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

  EEPROM.commit();
}
