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
const char CMD[4] = "cmd";
const char DT[3] = "dt";
const char UPDATE_SKETCH[7] = "sketch";
const char UPDATE_SPIFFS[7] = "spiffs";
char* UPDATE_ENDPOINT = "http://iot.flashvisions.com/api/public/update";
char* INIT_ENDPOINT = "http://iot.flashvisions.com/api/public/initialize";
const char AP_DEFAULT_PASS[10] = "iot@123!";
const unsigned int EEPROM_LIMIT = 512;
const unsigned long utcOffsetInSeconds = 19800; // INDIA
const unsigned int MQTT_MAX_CONNECT_TRIES = 2;
const unsigned int MAX_MESSAGES_STORE = 10;
const unsigned int MAX_MESSAGE_STORE_TIME_SECONDS = 5;

unsigned int eeAddress = 0;
unsigned int mqtt_connect_try = 0;
unsigned int message_counter = 0;

String IP;
boolean inited = false;
boolean mqtt_connected = false;

WiFiClient net;
WiFiManager wifiManager;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);


struct Settings {
  unsigned int valid = 1;
  unsigned int mqtt_port = 0;
  unsigned int mqtt_server_length = 0;
  char mqtt_server[50] = "0.0.0.0";
  unsigned int device_name_length = 0;
  char device_name[20] = "smartbell";
  unsigned int http_port = 80;
  unsigned int https_port = 443;
  unsigned long timestamp = 0;
  unsigned int reset = 0;
};

struct Settings conf = {};

struct Message {
  char id[20];
  char topic[100];
  char msg[100];
  char sub_source[20];
  boolean requires_ack = false;
  unsigned int qos = 0;
  boolean retain = false;
  unsigned int published = 0;
  unsigned int publish_error = 0;
  boolean ack_received = false;
  boolean is_received = false; // is this a sent message or received message
  unsigned int message_type = 0; // 0 = data & 1 = command
  unsigned long timestamp = 0;
};

struct Message messages[MAX_MESSAGES_STORE];

HTTPClient http;
WiFiClient espClient;
MQTTClient client;



void custom_setup()
{
  setupSensors();
}


void custom_loop()
{
  readSensor();
}


void setupSensors()
{
  pinMode(SOUND_PIN, INPUT);
}


void readSensor()
{
  int statusSensor = analogRead(SOUND_PIN);
  //Log.notice("VAL = %d" CR, statusSensor);
}



/**************************************************************************
 ******************* TEMPLATE METHODS START HERE **************************
***************************************************************************/


void publish_command(const char topic[], const char payload[], boolean requires_ack, unsigned int qos)
{
  publish_matter(topic, payload, 1, requires_ack, false, qos);
}


void publish_data(const char topic[], const char payload[], boolean requires_ack, boolean retain, unsigned int qos)
{
  publish_matter(topic, payload, 0, requires_ack, retain, qos);
}


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


void publish_matter(const char topic[], const char payload[])
{
  publish_matter(topic,payload, 0, false, false, 0);
}


void publish_matter(const char topic[], const char payload[], const unsigned int message_type)
{
  publish_matter(topic,payload, message_type, false, false, 0);
}


void publish_matter(const char topic[], const char payload[], const unsigned int message_type, boolean requires_ack)
{
  publish_matter(topic, payload, message_type, requires_ack, false, 0);
}


void publish_matter(const char topic[], const char payload[], const unsigned int message_type, boolean requires_ack, boolean retain)
{
  publish_matter(topic,payload,message_type, requires_ack, retain, 0);
}


void publish_matter(const char topic[], const char payload[], const unsigned int message_type, boolean requires_ack, boolean retain, unsigned int qos)
{
  if(message_counter < MAX_MESSAGES_STORE)
  {
    Message record = {};
    record.message_type = message_type;
    String idstring = generateMessageId();
    idstring.toCharArray(record.id, idstring.length() + 1);
    strcpy(record.topic, topic);
    Log.notice("record topic %s length %d" CR, record.topic, strlen(record.topic));
    strcpy(record.msg, payload);
    record.requires_ack = requires_ack;
    record.retain = retain;
    record.qos = qos;
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
  
  unsigned int i;
  unsigned int indices[10];
  unsigned int gc_counter = 0;
  unsigned long curr_timestamp = timeClient.getEpochTime();
  
  Log.trace("Total messages earlier = %d" CR, message_counter);
  
  for(i = message_counter ; i-- > 0;)
  {  
    Log.trace("ID = %s Index = %d, Current = %d , Record = %d" CR, messages[i].id, i, curr_timestamp, messages[i].timestamp);
    
    if(messages[i].published != 1)
    {
      if(mqtt_connected)
      {
        Log.trace("Publishing message" CR);
        
        if(publishNow(messages[i]))
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
        unsigned int diff = curr_timestamp - messages[i].timestamp;
        Log.trace("%d sec Old message record found at %d. Marking for removal" CR, diff, i);
        indices[gc_counter] = i;
        gc_counter++;
        
        Log.trace("gc_counter %d" CR, gc_counter);
      }
    }
  }

  cleanUpMessages(gc_counter, indices);
  Log.trace("Total messages later = %d" CR, message_counter);
}



void cleanUpMessages(unsigned int itemsToClean, unsigned int item_indices[])
{
  for(unsigned int i=0; i<itemsToClean;i++)
  {
    unsigned int pos = item_indices[i];
    Message message = messages[pos];
    
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


    if(message.publish_error == 1 || (message.requires_ack == 1 && message.ack_received != 1))
    {
      Log.notice("Topic %s message.requires_ack = %d message.ack_received = %d" CR, message.topic, message.requires_ack, message.ack_received);
      Log.trace("Rescheduling message for transmission" CR);
      publish_matter(message.topic, message.msg, message.requires_ack);
    }
  }
}



String buildFinalTopic(unsigned int msg_type, char topic[], char sub[])
{
  String topic_str = "";
  
  if(msg_type == 0)
  {
    if(strlen(sub) > 0)
    {
      topic_str = String(DT) + "/" + "smarthome" + "/" + String(conf.device_name) + "/" + String(sub) + String(topic);
    }
    else
    {
      topic_str = String(DT) + "/" + "smarthome" + "/" + String(conf.device_name) + String(topic);
    }
  }
  else
  {
    Log.trace("Not yet active!" CR);
    topic_str = String(CMD) + "/" + "smarthome" + "/" + String(conf.device_name) + "/" + "<destination-id>" + "/" + "<req-type>";    
  }

  return topic_str;
}


boolean publishNow(Message m)
{
  String topic_str = buildFinalTopic(m.message_type, m.topic, m.sub_source);
  String payload_str = "{}";

  if(m.message_type == 0)
  {
    payload_str = String(m.msg);
  }
  

  char final_topic[topic_str.length() + 1];
  topic_str.toCharArray(final_topic, topic_str.length() + 1);
  
  char final_payload[payload_str.length() + 1];
  payload_str.toCharArray(final_payload, payload_str.length() + 1);

  unsigned int payload_length = (unsigned)strlen(final_payload);
  Log.notice("Topic %s, Payload %s, Length %d" CR, final_topic, final_payload, payload_length);  
  
  return client.publish(final_topic, final_payload, payload_length, m.retain, m.qos);
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
      unsigned int code = root["code"];
      if (code == 200)
      {
        String mqtt_host = root["data"]["mqtt_host"];
        conf.mqtt_server[mqtt_host.length() + 1];
        mqtt_host.toCharArray(conf.mqtt_server, mqtt_host.length() + 1);

        String deviceNameStr = root["data"]["device_name"];
        conf.device_name [deviceNameStr.length() + 1];
        deviceNameStr.toCharArray(conf.device_name, deviceNameStr.length() + 1);

        conf.mqtt_port = root["data"]["mqtt_port"];
        //conf.http_port = root["data"]["http_port"];
        //conf.https_port = root["data"]["https_port"];
        conf.timestamp = root["data"]["timestamp"];

        conf.mqtt_server_length = sizeof(conf.mqtt_server);
        conf.device_name_length = sizeof(conf.device_name);
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


String macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}


String generateAPName()
{
  char ran[8];
  gen_random(ran, 8);

  return String(TYPE_CODE) + "-" + String(ran);
}

String generateMessageId()
{
  char ran[4];
  gen_random(ran, 4);
  return String(ran) + String(millis());
}


void gen_random(char *s, const unsigned int len) {
  static const char alphanum[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";

  for (unsigned int i = 0; i < len; ++i) {
    s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
  }

  s[len] = 0;
}


void setUpMqTTClient()
{
  client.begin(conf.mqtt_server, conf.mqtt_port, net);
  client.onMessage(messageReceived);
  
  declare_lastwill_testament();
}


void declare_lastwill_testament()
{
  char payload[] = "{\"online\":0}";
  String topic_str = buildFinalTopic(0, "/status", "");
  char willtopic[topic_str.length() + 1];
  topic_str.toCharArray(willtopic, topic_str.length() + 1);
  Log.trace("will topic %s" CR, willtopic);
  client.setWill(willtopic, payload, true, 1);
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
      Log.trace("Too many retries. Skipping..." CR);
      mqtt_connect_try = 0;
      return;
    }

    Log.notice("." CR);
    delay(2000);
  }

  Log.notice("Connected..." CR);
  mqtt_connected = true;

  // set state online
  declare_online();
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

  custom_setup();

  String NAME = generateAPName();
  char APNAME[NAME.length() + 1];
  NAME.toCharArray(APNAME, NAME.length() + 1);

  wifiManager.setAPStaticIPConfig(local_ip, gateway, subnet);
  wifiManager.autoConnect(APNAME, AP_DEFAULT_PASS);

  //if you get here you have connected to the WiFi
  Log.notice("connected...yeey :" CR);

  IP = WiFi.localIP().toString();
  Log.notice("My IP address: : %d.%d.%d.%d" CR, WiFi.localIP()[0],  WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
}


void declare_online()
{
  Log.notice("Online" CR);
  
  char payload[] = "{\"online\":1}";
  String topic_str = "/status";
  char topic[topic_str.length() + 1];
  topic_str.toCharArray(topic, topic_str.length() + 1);
  publish_matter(topic, payload, 0, false, true, 1);
}


void declare_offline()
{
  Log.notice("Offline" CR);
  
  char payload[] = "{\"online\":0}";
  String topic_str = "/status";
  char topic[topic_str.length() + 1];
  topic_str.toCharArray(topic, topic_str.length() + 1);
  publish_matter(topic, payload, 0, false, true, 1);
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
      /*
      char payload[] = "{\"msg\":\"hello\"}";
      String topic_str = "";
      char topic[topic_str.length() + 1];
      topic_str.toCharArray(topic, topic_str.length() + 1);
      publish_matter(topic, payload, 0, false, true, 1);
      */
      
      client.loop();
      delay(10);
    }
    
    processMessages();
    custom_loop();
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
  Log.notice("conf.mqtt_server %s" CR, conf.device_name);
  Log.notice("conf.timestamp %d" CR, conf.timestamp);
  Log.notice("conf.mqtt_server_length %d" CR, conf.mqtt_server_length);
  Log.notice("conf.mqtt_server_length %d" CR, conf.device_name_length);
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
      Log.notice("conf.mqtt_server %s" CR, conf.device_name);
      Log.notice("conf.timestamp %d" CR, conf.timestamp);
      Log.notice("conf.mqtt_server_length %d" CR, conf.mqtt_server_length);
      Log.notice("conf.mqtt_server_length %d" CR, conf.device_name_length);
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
