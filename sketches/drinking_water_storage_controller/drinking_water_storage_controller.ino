#include <TaskScheduler.h>

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
#define BEEPER 0 //D3



#define NOTICE_LIMIT 5

WiFiManager wm;
WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
std::unique_ptr<ESP8266WebServer> server;

boolean inited = false;

const char* APNAME="AQUA-001";
const char* AP_PASS="iotpassword";
const char* serverName = "http://iot.flashvisions.com";
const int DEFAULT_RUNTIME = 120; //seconds

String capabilities = "{\"name\":\"" + String(APNAME) + "\",\"devices\":{\"name\":\"Drinking Water storage Controller\",\"actions\":{\"getSwitch\":{\"method\":\"get\",\"path\":\"\/switch\/1\"},\"toggleSwitch\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\"},\"setSwitchOn\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\/on\"},\"setSwitchOff\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\/off\"}, \"getRuntime\":{\"method\":\"get\",\"path\":\"\/switch\/1\/runtime\"},\"setRuntime\":{\"method\":\"get\",\"path\":\"\/switch\/1\/runtime\",\"params\":[{\"name\":\"time\",\"type\":\"Number\",\"values\":\"60, 80, 100 etc\"}]}}},\"global\":{\"actions\":{\"getNotify\":{\"method\":\"get\",\"path\":\"\/notify\"},\"setNotify\":{\"method\":\"get\",\"path\":\"\/notify\/set\",\"params\":[{\"name\":\"notify\",\"type\":\"Number\",\"values\":\"1 or 0\"}]},\"getNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\"},\"setNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\/set\",\"params\":[{\"name\":\"url\",\"type\":\"String\",\"values\":\"http:\/\/google.com\"}]},\"reset\":\"\/reset\",\"info\":\"\/\"}}}";
String switch1state;
String subMessage;
String data;

boolean PUMP_EVENT = false;
boolean PUMP_RUN_REQUEST_TOKEN = false;
boolean TANK_FULL = false;
boolean SAFE_TO_RUN_PUMP = false;
boolean systemFault = false;
boolean beeping = false;
boolean debug = false;
boolean network = false;

const unsigned long CONSECUTIVE_PUMP_RUN_DELAY = 15000;
const unsigned long SENSOR_RECENT_TEST_THRESHOLD = 60000;
const unsigned long CONSECUTIVE_NOTIFICATION_DELAY = 5000;
const unsigned long SENSOR_STATE_CHANGE_THRESHOLD = 5000;
const unsigned long PUMP_SENSOR_STATE_CHANGE_THRESHOLD = 5000;
const unsigned long OVERFLOW_STATE_THRESHOLD = 5000;
const unsigned long SENSOR_TEST_THRESHOLD = 120000;
const unsigned long minInitialSensorReadTime = 10000;
const unsigned long minSensorTestReadtime = 10000;
const unsigned long TANK_FILLING_TIME_GAP = 120000;
unsigned long initialReadTime = 0;
unsigned long last_notify = 0;
unsigned long time_over_check;
unsigned long currentTimeStamp;
unsigned long lastTankFullDetect = 0;
unsigned long timeover;
unsigned long sensorTestTime = 0;
long max_runtime;

int echo = 1;
int error = 0;

boolean sensorCheck = false;
boolean sensorsInvert = false;
boolean BEEPING = false;

int low, high, pump;
unsigned long lastLowChange, lastHighChange, lastPumpChange, lastOverflowCondition;
int normalLow, normalHigh, invertLow, invertHigh;

int health = 1;
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
  int relay;
  int relay_runtime = 0;
  long relay_start = 0;
  long relay_stop = 0;
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


// Callback methods prototypes
void coreTask();

//Tasks
Task t1(1000, TASK_FOREVER, &coreTask);

Scheduler runner;




void configModeCallback (WiFiManager *myWiFiManager) 
{
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}




/**
 * Switches off relay(s)
 */
void switchOffRelay()
{
    Log.trace("Turning off relay");
    
    conf.relay=0;
    conf.relay_stop = millis();
    digitalWrite(RELAY_SWITCH, HIGH);
}



/**
 * Switches on relay(s)
 */
void switchOnRelay()
{
    Log.trace("Turning on relay");
  
    conf.relay=1;
    conf.relay_start = millis();
    digitalWrite(RELAY_SWITCH, LOW);
}



/**
 * Turn off beeper
 */
void switchOffBeeper()
{
  if(BEEPING == true)
  {
    Log.trace("Turning off beeper");
    digitalWrite(BEEPER, LOW);
    BEEPING = false;
  }
}



/**
 * Turn on beeper
 */
void switchOnBeeper()
{
  if(BEEPING == false)
  {
    Log.trace("Turning on beeper");
    digitalWrite(BEEPER, HIGH);
    BEEPING = true;
  }
}



/**
 * Checks composite relay state by reading the control pin(s). Both relays are required to switch the device on.
 */
boolean isRelayOn()
{
  int relay_state = digitalRead(RELAY_SWITCH);

  if(relay_state == 1)
  {
    return true;
  }
  else
  {   
    return false;
  }
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
    switchOffRelay();

    // Beeper
    pinMode(BEEPER, OUTPUT);    
    switchOffBeeper();


    /* INIT WIFI */

    gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP & event)
    {
      network = true;
      Log.notice("Station connected, IP: ");
      Serial.println(WiFi.localIP());
      init_server();
    });
    
    disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected & event)
    {
      network = true;
      Log.notice("Station disconnected!");
    });


    wm.setConfigPortalBlocking(false);
    wm.setAPCallback(configModeCallback);
    wm.setConfigPortalTimeout(180);    

    Log.notice("Attempting to connect to network using saved credentials" CR);  
    Log.notice("Connecting to WiFi..");
    
    
    boolean wm_connected = wm.autoConnect(APNAME, AP_PASS);
    if(wm_connected)
    {
      network = true;
    }else
    {
      network = false;
    }

    
    runner.init();
    Log.notice("Initialized scheduler" CR);
    
    runner.addTask(t1);
    Log.notice("Added core task to scheduler" CR);
    
    delay(5000);
    
    t1.enable();
    Log.notice("Enabled task t1" CR);

    //eraseSettings();
}



void init_server()
{
  server.reset(new ESP8266WebServer(WiFi.localIP(), 80));

  server->on("/", handleRoot);
  server->on("/reset", handleReset);
  server->on("/switch/1", readSwitch);
  server->on("/switch/1/set", toggleSwitch);
  server->on("/switch/1/set/on", switchAOn);
  server->on("/switch/1/set/off", switchAOff);
  server->on("/switch/1/runtime", getSwitchRuntime);
  server->on("/switch/1/runtime/set", setSwitchRuntime);
  server->on("/notify", getNotify);
  server->on("/notify/set", setNotify);
  server->on("/notify/url", getNotifyURL);
  server->on("/notify/url/set", setNotifyURL);
  
  server->onNotFound(handleNotFound);
  server->begin();
  
  Log.notice("HTTP server started" CR);
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


/**
 * Check to see if full sensor test was recently done or not
 */
boolean was_sensor_test_done_recently()
{
  if(lastSensorTest > 0 && (millis() - lastSensorTest <= SENSOR_RECENT_TEST_THRESHOLD))
  {
    return true;
  }
  else
  {
    return false;
  }  
}




/**
 * Checks water level condition using external float switch to determine whether pump can be run or not
 */
void relayConditionSafeGuard()
{
  currentTimeStamp = millis();
 
  /* prove that tank was not full recently (less than a day) or is not full right now */

  SAFE_TO_RUN_PUMP = true;

  if(!TANK_FULL)
  {
    // if it has been less than 2 minutes since tank was full. it cannot be very low yet!!
    if(currentTimeStamp - lastTankFullDetect < TANK_FILLING_TIME_GAP && lastTankFullDetect > 0)
    {
      Log.notice("Tank was recently full. Probably not the best time to run pump again!!");
      SAFE_TO_RUN_PUMP = false;
    }
  }
  

  /* If time over of liquid level recently full then stop */

  if(isPumpRunning())
  {
    max_runtime = conf.relay_runtime * 1000;
    timeover = ((currentTimeStamp - conf.relay_start) > max_runtime);

    Log.trace("currentTimeStamp - conf.relay_start = %d" CR, (currentTimeStamp - conf.relay_start));
    Log.trace("SAFE_TO_RUN_PUMP = %d | timeover = %d" CR, SAFE_TO_RUN_PUMP, timeover);
    
    if(!SAFE_TO_RUN_PUMP || timeover)
    { 
      stopPump();     
    }    
  }
}



/* Initialize settinsg and sensors */
void initialise()
{  
  // read top sensot
  tankState.pump = isPumpRunning();

  // read top sensot
  tankState.high = readSensor(TOP_DATA);

  // read bottom sensor
  tankState.low = readSensor(BOTTOM_DATA);

  
  // initial read time
  
  if(millis() - initialReadTime > minInitialSensorReadTime)
  {
      inited = true;
      Log.notice("Inited : " CR);      

      initSettings();

      
      String message = buildWaterLevelMessage(tankState);      
      notifyURL("System Reset!\n[" + message + "]", 0, 1);
      
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



/**
 * Core function
 */
void coreTask()
{
  Log.trace("Core task running" CR);
  
  if(!inited)
  {
    initialise();             
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
      relayConditionSafeGuard();
      evaluateTankState();
      takeActions();
    }
  }
  
  dispatchPendingNotification();
}


void loop() {
    runner.execute();
    wm.process();    

  
    if(WiFi.status()== WL_CONNECTED)
    {
      server->handleClient();
    }
}


/**
 * actions to be taken based on system state
 */
void takeActions()
{
  if(conf.reset == 1)
  {
    delay(5000);    
    eraseSettings();
    ESP.restart();
  }
  if(SYSTEM_ERROR == 1)
  {
    switchOnBeeper();
  }
  else
  {
    switchOffBeeper();

    if(willOverflow())
    {
      if(isPumpRunning())
      {
        Log.trace("Stopping pump to avoid overflow");
        stopPump();
      }
    }
    else if(willRunOutOfWaterSoon())
    {   
      if(!isPumpRunning())
      {
        if(was_sensor_test_done_recently())
        {
          Log.notice("Running pump to avoid running out of water");
          runPump();
        }
        else
        {
          Log.notice("Checking sensors before running pump");
          doSensorTest();
        }
      }
    }
    }  
}



/**
 * Check the running status of the pump by reading the acknowledgement pin
 */
boolean isPumpRunning()
{  
  if(pump == 1){
    return true;
  }else{
    return false;
  }
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
    high = readSensor(TOP_DATA);
    low = readSensor(BOTTOM_DATA);    

    Log.trace("=======================================================================" CR);
    Log.notice("Sensors : %d | %d | %d" CR, pump, high, low);
  
    // detect change
    trackSensorChanges(low, high, pump);   
  
  
    // update low level state
    if(hasLowChanged())
    {
      Log.notice("Low changed" CR);
      
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
      Log.notice("High changed" CR);
      
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
      Log.notice("Pump changed" CR);
      
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

    //monitor full
    trackTankFull(tankState.low, tankState.high);

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



boolean willRunOutOfWaterSoon()
{
  if(INSUFFICIENTWATER == 1)
  {
    return true;
  }
  else
  {
    return false;
  }
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



boolean isTankFull(){
  if(low == 1 && high == 1)
  {
    return true;
  }
  else
  {
    return false;
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



void trackSensorChanges(int &low, int &high, int &pump)
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


  if(pump != tankState.pump)
  {
    if(lastPumpChange == 0)
    {
      lastPumpChange = currentTimeStamp;
    }
  }
  else
  {
    lastPumpChange = 0;
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



void trackTankFull(int low, int high)
{
  currentTimeStamp = millis();
  
  // track overflow
  if(low == 1 && high == 1)
  {
    Log.trace("Tank full " CR);  

    if(TANK_FULL == false)
    {
      TANK_FULL = true;

      if(lastTankFullDetect == 0)
      {
        lastTankFullDetect = currentTimeStamp;
      }
    }
  }
  else
  {
    if(TANK_FULL == true)
    {
      TANK_FULL = false;
      lastTankFullDetect = 0;
    }
  }
}



void checkAndRespondToRelayConditionSafeGuard()
{
  if(!SAFE_TO_RUN_PUMP)
  {
     server->send(400, "text/plain", "SAFE_TO_RUN_PUMP=FALSE");
     return;
  }
}



/**
 * Handle root visit
 */
void handleRoot() {
  server->send(200, "application/json", capabilities);
}




/**
 * Handle reset request
 */
void handleReset() 
{
  conf.reset = 1;
  server->send(200, "text/plain", "Resetting in 5 seconds");
}




/**
 * Handle non existent path
 */
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




/**
 * Gets pump runtime
 */
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




/**
 * Sets pump runtime in seconds
 */
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



/**
 * Reads pump switch state
 */
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




/**
 * Toggles pump state
 */
void toggleSwitch()
{
  checkAndRespondToRelayConditionSafeGuard();

  if(conf.relay == 0)
  {
    PUMP_RUN_REQUEST_TOKEN = true;
    
    runPump();
    switch1state="STATE=ON";
  }
  else
  {
    stopPump();
    switch1state="STATE=OFF";
  }

  server->send(200, "text/plain", switch1state);
}





/**
 * Requests swutching the pump on
 */
void switchAOn()
{
  checkAndRespondToRelayConditionSafeGuard();
  
  if(conf.relay == 0)
  {
    PUMP_RUN_REQUEST_TOKEN = true;
    runPump();
  }
    
  server->send(200, "text/plain", "STATE=ON");
}




/**
 * Requests swutching the pump off
 */
void switchAOff()
{
  checkAndRespondToRelayConditionSafeGuard();
  stopPump();

  server->send(200, "text/plain", "STATE=OFF");
}




/**
 * Gets the http(s) Notification permission
 */
void getNotify()
{
  readSettings();

  int notify = conf.notify;
  server->send(200, "text/plain", "NOTIFY=" + String(notify));
}




/**
 * Sets the http(s) Notification permission
 */
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
      server->send(200, "text/plain", "NOTIFY=" + String(conf.notify));      
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



/**
 * Gets the http(s) Notification url endpoint
 */
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




/**
 * Sets the http(s) Notification url endpoint
 */
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

   // if no network then no need to enque notifications
   if(network && queue.count() < NOTICE_LIMIT){
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
      post+= "aqua_001=1";
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

        Log.notice("Popping notification from queue. Current size = %d" CR, queue.count());

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




/**
 * Start the pump operation by switching on the relay
 */
void runPump()
{
   String msg;

   if(conf.relay == 0)
   {
      if(millis() - conf.relay_start > CONSECUTIVE_PUMP_RUN_DELAY)
      {
        Log.notice("Starting pump!" CR);
        switchOnRelay();
        pump = 1;
      }
      else
      {
        msg = "Pump was last run very recently. It cannot be run consecutively. Try after some time!";
        Log.notice("%s" CR, msg);
        notifyURL(msg);
      }
   }
   else
   {
        msg = "Pump cannot be started now as it was already started or is still running and has not stopped automatically!";
        Log.error("%s" CR, msg);
        notifyURL(msg);
   }
}



/**
 * Stops pump by switching off the relay
 */
void stopPump()
{
  if(conf.relay == 1)
  { 
    Log.notice("Stopping pump!" CR);
    switchOffRelay();
    pump = 0;
  }
}




/**
 * Initialize application settings. read eeprom to see if this is first run. if yes write with defaults.
 */
void initSettings()
{
  Log.trace("init Settings" CR); 
  
  readSettings();
  
  if (conf.timestamp<1) 
  {
    Log.notice("Setting defaults" CR);
    
    String url = "iot.flashvisions.com";
    char tmp[url.length() + 1];
    url.toCharArray(tmp, url.length() + 1);

    conf.endpoint_length = url.length();
    memset(conf.endpoint, 0, sizeof(conf.endpoint));
    strncpy(conf.endpoint, tmp, strlen(tmp));
    
    conf.notify = 1;
    conf.relay = 0;
    conf.relay_runtime = DEFAULT_RUNTIME;
    conf.relay_start = 0;
    conf.relay_stop = 0;
    conf.reset = 0;
    conf.timestamp = 0;
  }


  // save settings
  writeSettings();
}




/**
 * Write settings object to eeprom
 */
void writeSettings()
{
  EEPROM.begin(EEPROM_LIMIT);
  eeAddress = 0;
  
  conf.timestamp = millis();

  EEPROM.write(eeAddress, conf.relay);
  eeAddress++;  
  EEPROM.write(eeAddress, conf.relay_runtime);
  eeAddress++;
  EEPROM.write(eeAddress, conf.relay_start);
  eeAddress++;
  EEPROM.write(eeAddress, conf.relay_stop);
  eeAddress++;
  EEPROM.write(eeAddress, conf.reset);
  eeAddress++;
  EEPROM.write(eeAddress, conf.notify);
  eeAddress++;
  EEPROM.write(eeAddress, conf.timestamp);
  eeAddress++;
  EEPROM.write(eeAddress, conf.endpoint_length);
  eeAddress++;
  
  writeEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);+

  EEPROM.commit();
  EEPROM.end();

  Log.notice("Conf saved" CR);
}





/**
 * Write a string to eeprom starting at {startAdr}
 */
void writeEEPROM(int startAdr, int len, char* writeString) {
  //yield();
  for (int i = 0; i < len; i++) {
    EEPROM.write(startAdr + i, writeString[i]);
  }
}



/**
 * Read settings object from eeprom
 */
void readSettings()
{
  EEPROM.begin(EEPROM_LIMIT);
  eeAddress = 0;

  conf.relay = EEPROM.read(eeAddress);
  eeAddress++;
  conf.relay_runtime = EEPROM.read(eeAddress);
  eeAddress++;
  conf.relay_start = EEPROM.read(eeAddress);
  eeAddress++;
  conf.relay_stop = EEPROM.read(eeAddress);
  eeAddress++;
  conf.reset = EEPROM.read(eeAddress);
  eeAddress++;
  conf.notify = EEPROM.read(eeAddress);
  eeAddress++;
  conf.timestamp = EEPROM.read(eeAddress);
  eeAddress++;
  conf.endpoint_length = EEPROM.read(eeAddress);

  eeAddress++;
  readEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  EEPROM.end();

  Log.notice("Conf read" CR);
}



/**
 * Read a string from eeprom
 */
void readEEPROM(int startAdr, int maxLength, char* dest) {

  for (int i = 0; i < maxLength; i++) {
    dest[i] = char(EEPROM.read(startAdr + i));
  }
}



/**
 * Erases eeprom data by writing zeros
 */
void eraseSettings()
{
  EEPROM.begin(EEPROM_LIMIT);
  
  Log.notice("Erasing eeprom" CR);  
  for (int i = 0; i < EEPROM_LIMIT; i++){
    EEPROM.write(i, 0);
  }

  EEPROM.commit();
  EEPROM.end();
}
