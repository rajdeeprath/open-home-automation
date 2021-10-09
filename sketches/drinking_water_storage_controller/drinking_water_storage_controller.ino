#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <QueueArray.h>
#include <ArduinoLog.h>
#include <WiFiClient.h>

#define TOP_LEVEL 4 //D2
#define TOP_DATA 14 //D5

#define BOTTOM_LEVEL 5 //D1
#define BOTTOM_DATA 12 //D6

#define RELAY_SWITCH 13 //D7

#define NOTICE_LIMIT 5

WiFiManager wm;


boolean inited = false;
long initialReadTime = 0;
long minInitialSensorReadTime = 15000;
long minSensorTestReadtime = 15000;

const String NAME="AQUA-P-001";
const char* serverName = "http://iot.flashvisions.com/?amu_pc_001=1";


String subMessage;
String data;

boolean PUMP_EVENT = false;
long last_notify = 0;
long currentTimeStamp;
boolean systemFault;
boolean beeping;
boolean debug = false;
int echo = 1;
int error = 0;

const long CONSECUTIVE_NOTIFICATION_DELAY = 5000;
const long SENSOR_STATE_CHANGE_THRESHOLD = 60000;
const long PUMP_SENSOR_STATE_CHANGE_THRESHOLD = 10000;
const long OVERFLOW_STATE_THRESHOLD = 300000;
const long SENSOR_TEST_THRESHOLD = 120000;

boolean sensorCheck = false;
long sensorTestTime = 0;
boolean sensorsInvert = false;

int low, high, pump;
long lastLowChange, lastHighChange, lastPumpChange, lastOverflowCondition;
int normalLow, normalHigh;
int invertLow, invertHigh;


int health = 1;
boolean SENSOR_TEST_EVENT = false;
int INSUFFICIENTWATER = 0;
int SYSTEM_ERROR = 0;
long lastSensorTest = 0;
boolean isPumpSensorNpN = true;


const int EEPROM_LIMIT = 512;
int eeAddress = 0;


struct TankState {
   int low = 0;
   int high = 0;
   int pump = 0;
   long lastupdate = 0;
};


struct Settings {
  int notify = 1;
  long timestamp;
  int endpoint_length;
  int reset = 0;
  char endpoint[50] = "";
};


struct Notification {
   int low;
   int high;
   int pump;
   int health;
   int echo;
   long queue_time = 0;
   long send_time = 0;
   char message[80] = "";
   int error=0;
   int debug=0;
};

HTTPClient http;
WiFiClient wifiClient;

QueueArray <Notification> queue;
boolean posting;
boolean stateChanged = false;

Settings conf = {};
TankState tankState = {};


void configModeCallback (WiFiManager *myWiFiManager) 
{
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}




void setup() {

  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP 
    
    Serial.begin(115200);
    Log.begin(LOG_LEVEL_NOTICE, &Serial);
    
    Log.notice("Preparing to start" CR);


    /* INIT PINS */   

    // top sensor
    pinMode(TOP_LEVEL, OUTPUT);//level
    pinMode(TOP_DATA, INPUT);//data  
  
    // bottom sensor
    pinMode(BOTTOM_LEVEL, OUTPUT);//level
    pinMode(BOTTOM_DATA, INPUT);//data

    normalizeSensorLevels();

    pinMode(RELAY_SWITCH, OUTPUT);
    relay_switch_off();


    /* INIT WIFI */

    wm.setConfigPortalBlocking(false);
    wm.setAPCallback(configModeCallback);
    wm.setConfigPortalTimeout(180);    

    Log.notice("Attempting to connect to network using saved credentials" CR);  
    Log.notice("Connecting to WiFi..");
    
    
    if(wm.autoConnect("AQUA-P-001", "iot@123"))
    {
      Log.notice("Connected to WiFi");
      delay(2000);
    }
    else 
    {
        Log.notice("Configportal running");
    }
}



void relay_switch_on()
{
  Log.notice("Switching on relay" CR);
  digitalWrite(RELAY_SWITCH, LOW);
}


void relay_switch_off()
{
  Log.notice("Switching off relay" CR);
  digitalWrite(RELAY_SWITCH, HIGH);
}




int readSensor(int pin)
{  
  if(pin == BOTTOM_DATA && isPumpSensorNpN == true)
  {
    int state = digitalRead(pin);
    
    if(state == 1)
    {
      return 0;
    }
    else
    {
      return 1;
    }
  }
  else
  {
    return digitalRead(pin);
  }
}



/* Start a full sensor test */
void doSensorTest()
{  
  sensorCheck = true;
  sensorTestTime = millis();
  
  Log.notice("Starting sensor test" CR);  
  normalizeSensorLevels();  
}



/**
 * Set upright logic level signal for all sensors (HIGH)
 */
void normalizeSensorLevels()
{
  sensorsInvert = false;   
  
  Log.notice("Sensor levels normalized" CR);
  
  digitalWrite(TOP_LEVEL, HIGH); // level
  digitalWrite(BOTTOM_LEVEL, HIGH); // level
}




/**
 * Invert logic level signal for all sensors (LOW)
 */
void invertSensorLevels()
{  
  sensorsInvert = true;
  
  Log.notice("Sensor levels inverted" CR);
    
  digitalWrite(TOP_LEVEL, LOW); // level
  digitalWrite(BOTTOM_LEVEL, LOW); // level
}



/**
 * End / cancel sensor test
 */
void terminateSensorTest()
{  
  sensorCheck = false;  
  sensorsInvert = false;
  sensorTestTime = millis();

  Log.notice("Stopping sensor test" CR);
  
  normalizeSensorLevels();
}


/**
 * Test sesnors normally first and then by inverting. Inverted lopic level should produce inverted output
 */
void testSensors()
{
  if(!sensorsInvert)
  {
    Log.notice("Checking normal sensor states" CR);
      
    // read top sensor
    normalHigh = readSensor(TOP_DATA);
    
    // read bottom sensor
    normalLow = readSensor(BOTTOM_DATA);

    Log.notice("Sensors : %d | %d" CR, normalHigh, normalLow);
  
    // change condition after minSensorTestReadtime seconds (read sensor output for `minSensorTestReadtime` ms)
    if(millis() - sensorTestTime > minSensorTestReadtime){ 
      sensorTestTime = millis();
      invertSensorLevels();      
    }
  }
  else 
  {
    Log.notice("Checking invert sensor states" CR);

    // read top sensor
    invertHigh = readSensor(TOP_DATA);
    
    // read bottom sensor
    invertLow = readSensor(BOTTOM_DATA);

    Log.notice("Sensors : %d | %d" CR, invertHigh, invertLow);
    
    
    // change condition after minSensorTestReadtime seconds
    
    if(millis() - sensorTestTime > minSensorTestReadtime)
    {
      // finish test
      terminateSensorTest();  

      // record last test time
      lastSensorTest = millis();

      // evaluate result
      if(normalLow != invertLow && normalHigh != invertHigh)
      {
        health = 1;
        Log.notice(" SENSORS OK " CR);
      }
      else
      {
        String sensorReport = "Sensors problem detected!";
        sensorReport = sensorReport + "\n\r";
        sensorReport = sensorReport + "NL="+normalLow+",IN="+invertLow+",NH="+normalHigh+",IH="+invertHigh;
        
        Log.notice(" SENSORS ERROR " CR);

        health = 0;        
      }
    }
  }
}




/* First read of sensors as soon as system starts */
void initSensors()
{
  
  // read bottom sensor
  tankState.low = readSensor(BOTTOM_DATA);

  // read top sensot
  tankState.high = readSensor(TOP_DATA);
  // initial read time
  
  if(millis() - initialReadTime > minInitialSensorReadTime)
  {
      inited = true;
      Log.notice("Inited : " CR);

      doSensorTest();
  }
}



String buildWaterLevelMessage(TankState &tankState)
{
  String message = "";

  if(tankState.high == 1)
  {
    message = "Water Level @ 100%";
  }
  else if(tankState.low == 1)
  {
    message = "Water Level between 10% to 90%";
  }
  else
  {
    message = "Water Level Critical! (less than 10%)";
  }

  return message;
}


void loop() {  

    wm.process();    
    
    if(!inited)
    {
      initSensors();  
      initSettings();      
    }
    else if(sensorCheck)
    {
      testSensors();
    }
    else
    {
      if(health == 1)
      {
        Log.trace("Health OK" CR);
        evaluateTankState();
      }
    }

    dispatchPendingNotification();
    delay(1000);
}




/**
 * Evaluates realtime state of water storage container
 **/ 
void evaluateTankState()
{
    subMessage = "";
    stateChanged = false;
    error = 0;
    
    // read sensor data
    low = readSensor(BOTTOM_DATA);
    high = readSensor(TOP_DATA);

    Log.trace("=======================================================================" CR);
    Log.trace("Sensors : %d | %d" CR, low, high);
  
    // detect change
    trackSensorChanges(low, high);   
  
  
    // update low level state
    if(hasLowChanged())
    {
      if(low == 1)
      {
        if(pump == 1)
        {
          subMessage = "Storage has started filling";
        }
        else
        {
          error = 1;
          subMessage = "Unexpected state change of 'low' sensor";
        }
      }
      else
      {
        if(high == 0)
        {
          subMessage = "Water Level too low!! ";
        }
        else
        {
          error = 1;
          subMessage = "Unexpected state change of 'low' sensor";
        }
      }

      if(error == 0){
        stateChanged = true;
        tankState.low = low;
      }
    }  
  
  
    // update high level state
    if(hasHighChanged())
    {
      if(high == 1)
      {
        if(low == 1 && pump ==1)
        {
          subMessage = "Storage is full!! ";
        }
        else
        {
          error = 1;
          subMessage = "Unexpected state change of 'high' sensor";
        }
      }
      else
      {
        subMessage = "Water consumption started";
      }

      if(error == 0){
        stateChanged = true;
        tankState.high = high;
      }
    }  
  
  
    // update pump level state
    if(hasPumpChanged())
    {
      String levelInfo = buildWaterLevelMessage(tankState);
      if(pump == 1)
      {
        subMessage = "Aquaguard Started!\n[" + levelInfo + "]";
      }
      else
      {
        subMessage = "Aquaguard Stopped!\n[" + levelInfo + "]";
      }
      
      stateChanged = true;
      tankState.pump = pump;
    }

    // monitor overflow
    trackOverFlow(tankState.pump, tankState.high);

    // monitor undrflow
    trackInsufficientWater(tankState.low, tankState.high, tankState.pump);

    // track error
    trackSystemError(error);
    
  
    /***************************/ 
    Log.trace("Sensors : %d | %d | %d" CR, tankState.pump, tankState.high, tankState.low);
    Log.trace("=======================================================================" CR);
    Log.trace("State changed = %T" CR, stateChanged);


    // evaluate and dispatch message
    if(stateChanged)
    {
      String message = "";

      if(subMessage == "")
      {
        // evaluate
        message = buildWaterLevelMessage(tankState);

        // dispatch
        if(message != "")
        {
          if(error == 0)
          {
            notifyURL(message);
          }
          else
          {
            notifyURL(message, 1);
          }
        }
      }
      else
      {
        if(error == 0)
        {
          notifyURL(subMessage);
        }
        else
        {
          notifyURL(subMessage, 1);
        }
      }   
    }
}


boolean hasLowChanged()
{
  currentTimeStamp = millis();
  return ((currentTimeStamp - lastLowChange) > SENSOR_STATE_CHANGE_THRESHOLD && lastLowChange > 0);
}


boolean willOverflow()
{
  currentTimeStamp = millis();
  return ((currentTimeStamp - lastOverflowCondition) > OVERFLOW_STATE_THRESHOLD && lastOverflowCondition > 0);
}



boolean hasHighChanged()
{
  currentTimeStamp = millis();
  return ((currentTimeStamp - lastHighChange) > SENSOR_STATE_CHANGE_THRESHOLD && lastHighChange > 0);
}


boolean hasPumpChanged()
{
  currentTimeStamp = millis();
  return ((currentTimeStamp - lastPumpChange) > PUMP_SENSOR_STATE_CHANGE_THRESHOLD && lastPumpChange > 0);
}


void trackSystemError(int &error)
{
  if(error == 1)
  {
    SYSTEM_ERROR = 1;
  }
  else
  {
    SYSTEM_ERROR = 0;  
  }
}


void trackSystemError(int &error, String message)
{
  if(error == 1)
  {
    if(SYSTEM_ERROR == 0){
      SYSTEM_ERROR = 1;
      notifyURL(message, 1);
    }
  }
  else
  {
    SYSTEM_ERROR = 0;  
  }
}


void trackInsufficientWater(int &low, int &high, int &pump)
{
  if(low == 0 && high == 0 && pump == 0)
  {
    INSUFFICIENTWATER = 1;
  }
  else
  {
    INSUFFICIENTWATER = 0;
  }
}



void trackSensorChanges(int &low, int &high)
{
  currentTimeStamp = millis();
  
  if(low != tankState.low)
  {
    if(lastLowChange == 0)
    {
      lastLowChange = currentTimeStamp;
    }
  }
  else
  {
    lastLowChange = 0;
  }


  if(high != tankState.high)
  {
    if(lastHighChange == 0)
    {
      lastHighChange = currentTimeStamp;
    }
  }
  else
  {
    lastHighChange = 0;
  }
}



void trackOverFlow(int pump, int high)
{
  currentTimeStamp = millis();
  
  // track overflow
  if(pump == 1 && high == 1)
  {
    Log.trace("Overflow condition" CR);  
    
    if(lastOverflowCondition == 0)
    {
      lastOverflowCondition = currentTimeStamp;
    }
  }
  else
  {
    lastOverflowCondition = 0;
  }
}





/**
 * Add to Notification queue
 */
void notifyURL(String message)
{
  notifyURL(message, 0);
}



/**
 * Generate and push notification with message, error flag
 */
void notifyURL(String message, int error)
{
  notifyURL(message, error, 0);
}




/**
 * Generate and push notification with message, error flag and debug flag
 */
void notifyURL(String message, int error, int debug)
{
  Log.trace("Preparing notification" CR);
  
  Notification notice = {};
  notice.low = tankState.low;
  notice.high = tankState.high;
  notice.pump = tankState.pump;
  message.toCharArray(notice.message, 80);
  notice.health = health;
  notice.echo = echo;
  notice.error = error;
  
  enqueueNotification(notice);
}



/* Add to Notification queue */
void enqueueNotification(struct Notification notice)
{
   notice.queue_time = millis();

   if(queue.count() < NOTICE_LIMIT){
    Log.trace("Pushing notification to queue" CR);
    queue.enqueue(notice);
   }
}




/**
 * Prepare notification string object to send to remote server
 */
String getPostNotificationString(Notification &notice)
{
      String post = "";
      post+="amu_pc_001=1";
      post+="&";
      post+="message="+String(notice.message);
      post+="&";
      post+="health="+String(notice.health);
      post+="&";
      post+="echo="+String(notice.echo);
      post+="&";
      post+="low="+String(notice.low);
      post+="&";
      post+="high="+String(notice.high);
      post+="&";
      post+="pump="+String(notice.pump);
      post+="&";
      post+="error="+String(notice.error);
      post+="&";
      post+="debug="+String(notice.debug);     

      return post;
}




/**
 * Send http(s) Notification to remote url with appropriate parameters and custom message
 */
void dispatchPendingNotification()
{
  if(currentTimeStamp - last_notify > CONSECUTIVE_NOTIFICATION_DELAY)
  {    
    if (!posting && conf.notify == 1 && !queue.isEmpty())
    {
      Log.trace("Running Notification service" CR);

      if(WiFi.status()== WL_CONNECTED)
      { 
        posting = true;
        
        Notification notice;

        Log.trace("Popping notification from queue. Current size = %d" CR, queue.count());

        notice = queue.dequeue();        
        notice.send_time = millis();
        data = getPostNotificationString(notice);               
        http.begin(wifiClient, serverName);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        http.addHeader("Host", "iot.flashvisions.com");
        http.addHeader("Content-Length", String(data.length()));
        int httpResponseCode = http.POST(data);       
        Log.trace("HTTP Response code: %d" CR, httpResponseCode);
        http.end();
      }
      else 
      {
        Log.error("WiFi not connected, cannot post data" CR);
      }
      
      posting = false;
      last_notify = currentTimeStamp;
    }
  }  
}




void initSettings()
{
  readSettings();
  
  if (conf.timestamp<1) 
  {
    Log.trace("Setting defaults" CR);
    
    String url = "0.0.0.0";
    char tmp[url.length() + 1];
    url.toCharArray(tmp, url.length() + 1);

    conf.endpoint_length = url.length();
    memset(conf.endpoint, 0, sizeof(conf.endpoint));
    strncpy(conf.endpoint, tmp, strlen(tmp));
    
    conf.notify = 0;
  }

  // save settings
  writeSettings();
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
  EEPROM.write(eeAddress, conf.reset);  

  eeAddress++;
  writeEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  EEPROM.commit();

  Log.notice("Conf saved" CR);
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
  conf.reset = EEPROM.read(eeAddress);

  eeAddress++;
  readEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  Log.notice("Conf read" CR);
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
