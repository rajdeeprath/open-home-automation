#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"         //https://github.com/tzapu/WiFiManager
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <QueueArray.h>

#define RELAY1 4 
#define RELAY2 5 
#define RELAY1_READER 12
#define RELAY2_READER 14
#define LIQUID_LEVEL_SENSOR A0 
#define LED 16 
#define BEEPER 13 
#define PUMP_SENSOR 15

#define NOTICE_LIMIT 5

const String NAME="HMU-PC-001";

int DEFAULT_RUNTIME = 30;
long max_runtime;
long system_start_time;
long wait_time = 5000;
boolean debug=true;
boolean inited = false;
boolean timeover;
int eeAddress = 0;
int liquidLevelSensorReadIn = 0;
int liquidLevelSensorReadInThreshold = 950;
String switch1state;
boolean LIQUID_LEVEL_OK = true;
boolean PUMP_CONNECTION_ON = false;
boolean PUMP_RUN_REQUEST_TOKEN = false;

long last_notify = 0;
long accidentGuardLastRun = 0;
boolean PIN_ERROR = false;
long time_over_check;
long lastBeepStateChange;
boolean systemFault;
boolean beeping;

long INIT_DELAY = 15000;
long CONSECUTIVE_NOTIFICATION_DELAY = 5000;
long CONSECUTIVE_PUMP_RUN_DELAY = 15000;
long OPERATION_FEEDBACK_CHECK_DELAY = 2000;
long ACCIDENT_GUARD_RUN_DELAY = 5000;
long BEEPER_DELAY = 1500;

String capailities = "{\"name\":\"" + NAME + "\",\"devices\":{\"name\":\"Irrigation Pump Controller\",\"actions\":{\"getSwitch\":{\"method\":\"get\",\"path\":\"\/switch\/1\"},\"toggleSwitch\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\"},\"setSwitchOn\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\/on\"},\"setSwitchOff\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\/off\"}, \"getRuntime\":{\"method\":\"get\",\"path\":\"\/switch\/1\/runtime\"},\"setRuntime\":{\"method\":\"get\",\"path\":\"\/switch\/1\/runtime\",\"params\":[{\"name\":\"time\",\"type\":\"Number\",\"values\":\"60, 80, 100 etc\"}]}}},\"global\":{\"actions\":{\"getNotify\":{\"method\":\"get\",\"path\":\"\/notify\"},\"setNotify\":{\"method\":\"get\",\"path\":\"\/notify\/set\",\"params\":[{\"name\":\"notify\",\"type\":\"Number\",\"values\":\"1 or 0\"}]},\"getNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\"},\"setNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\/set\",\"params\":[{\"name\":\"url\",\"type\":\"String\",\"values\":\"http:\/\/google.com\"}]},\"reset\":\"\/reset\",\"info\":\"\/\"}}}";

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

struct Notification {
   int relay;
   int relay_runtime;
   long relay_start;
   long relay_stop;
   char message[80] = "";
   long queue_time;
   long send_time;
};

QueueArray <Notification> queue;
//struct Notification queue[NOTICE_LIMIT];
//int queueIndex = 0;

Settings conf = {};

WiFiManager wifiManager;
std::unique_ptr<ESP8266WebServer> server;

HTTPClient http;
boolean posting;




/**
 * Handle root visit
 */
void handleRoot() {
  server->send(200, "application/json", capailities);
}




/**
 * Handle reset request
 */
void handleReset() 
{
  conf.reset = 1;
  writeSettings();
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
    PUMP_RUN_REQUEST_TOKEN = false;
    
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

  PUMP_RUN_REQUEST_TOKEN = false;
  
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
  debugPrint("Preparing notification");
  
  Notification notice = {};
  notice.relay = conf.relay;
  notice.relay_runtime = conf.relay_runtime;
  notice.relay_start = conf.relay_start;
  notice.relay_stop = conf.relay_stop;
  message.toCharArray(notice.message, 80);
  notice.queue_time = 0;
  notice.send_time = 0;
  
  enqueueNotification(notice);
}


/* Add to Notification queue */
void enqueueNotification(struct Notification notice)
{
   notice.queue_time = millis();

   debugPrint("Enquing notification");

   if(queue.count() < NOTICE_LIMIT){
    debugPrint("Pushing notification to queue");
    queue.enqueue(notice);
   }
}




/**
 * Send http(s) Notification to remote url with appropriate parameters and custom message
 */
void dispatchPendingNotification()
{
  if(millis() - last_notify > CONSECUTIVE_NOTIFICATION_DELAY)
  {    
    if (!posting && conf.notify == 1 && !queue.isEmpty())
    {
      debugPrint("Running Notification service");

      debugPrint("Popping notification from queue. Current size = " + String( queue.count()));
      Notification notice = queue.dequeue();
      notice.send_time = millis();
  
      posting = true;
  
      http.begin(String(conf.endpoint));
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
      int httpCode = http.POST("pump=" + String(notice.relay) + "&run_time" + String(notice.relay_runtime) + "&relay_start" + String(notice.relay_start) + "&relay_stop" + String(notice.relay_stop) + "&queue_time=" + String(notice.queue_time) + "&send_time=" + String(notice.send_time) +   "&message=" + notice.message);
      debugPrint(String(httpCode));
  
      http.end();
  
      posting = false;
      last_notify = millis();
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
        debugPrint("Starting pump!");
        switchOnCompositeRelay();
      }
      else
      {
        msg = "Pump was last run very recently. It cannot be run consecutively. Try after some time!";
        debugPrint(msg);
        notifyURL(msg);
      }
   }
   else
   {
        msg = "Pump cannot be started now as it was already started or is still running and has not stopped automatically!";
        debugPrint(msg);
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
    debugPrint("Stopping pump!");
    switchOffCompositeRelay();
  }
}



/**
 * Prevents accidental runs of the pump controller under unexpected component failures or runaways logic.
 */
void preventUnauthorizedRun()
{
    String msg;
    boolean RED_FLAG;
    long lastRunElapsedTime = (millis() - conf.relay_start);
    long lastStopElapsedTime = (millis() - conf.relay_stop);
    boolean canCheck = ((lastRunElapsedTime > OPERATION_FEEDBACK_CHECK_DELAY) && (lastStopElapsedTime > OPERATION_FEEDBACK_CHECK_DELAY) && (millis() - accidentGuardLastRun > ACCIDENT_GUARD_RUN_DELAY));

    if(canCheck)
    {
        //debugPrint("Checking to see if a accidental run condition exists...");
        
        /* Check for possible fault */    
        if(!PUMP_RUN_REQUEST_TOKEN)
        {
          debugPrint("Running accident prevention & warning routine checks!!");         
          
          if(PUMP_CONNECTION_ON) // Check feedback
          {
            RED_FLAG = true;
            msg  = "WARNING : Pump connection is on without request!!. Attempting to block";
          }
          else if(isCompositeRelayOn()) // Check relay pin(s) state(s)
          {
            RED_FLAG = true;
            msg  = "WARNING : Pump relay state is on without request!!. Attempting to block";
          }
          else if(PIN_ERROR)
          {
            RED_FLAG = true;
            msg  = "WARNING : There seems to be a pin error. One or more relay pins are in an inconsistent state!!.";
          }
        }
        else
        {
          RED_FLAG = false;
        }

        
  
        /* Take protective measure if any */
        if(RED_FLAG)
        {
          systemFault = true;
          
          debugPrint(msg);      
          notifyURL(msg); 
        }
        else
        {
          systemFault = false; 
        }


        accidentGuardLastRun = millis();
    }   
}


/**
 * Captures the actual pump connection status. This is enabled only if the composite relay is on and the AC current is flowing through it.
 * The BT100 current sensor will transmit a +3.3v dc to the microcontroller to indicate a working circuit.
 */
void checkPumpRunningStatus(){

    String msg;
    
    if(!isPumpRunning())
    {
      if(PUMP_CONNECTION_ON){
        PUMP_CONNECTION_ON = false;
        msg = "Pump stopped!";
        debugPrint(msg);
        
        // notify status
        notifyURL(msg);
      }
    }
    else
    {
      if(!PUMP_CONNECTION_ON){        
        PUMP_CONNECTION_ON = true;
        msg = "Pump running!";
        debugPrint(msg);

        // notify status
        notifyURL(msg);
      }
    }
}



/**
 * Switches water level indicator led on
 */
void ledOn()
{
  conf.led=1;
  digitalWrite(LED, HIGH); 
}



/**
 * Switches water level indicator led off
 */
void ledOff()
{
  conf.led=0;
  digitalWrite(LED, LOW); 
}



/**
 * Alternate beeps
 */
void beepOnFault()
{
  if(systemFault)
  {
    if(millis() - lastBeepStateChange > BEEPER_DELAY)
    {
      if(beeping)
      {
        beeperOff();
      }
      else
      {
        beeperOn();
      }
      
      lastBeepStateChange = millis();
    }    
  }
  else
  {
    if(beeping)
    {
      beeperOff();
    }
  }
}




/**
 * Turns on beeper pin
 */
void beeperOn()
{
  digitalWrite(BEEPER, HIGH); 
  beeping = true;
}



/**
 * Turns off beeper pin
 */
void beeperOff()
{
  digitalWrite(BEEPER, LOW);
  beeping = false; 
}



void checkAndRespondToRelayConditionSafeGuard()
{
  if(!LIQUID_LEVEL_OK)
  {
     server->send(400, "text/plain", "LIQUID_LEVEL_OK=FALSE");
     return;
  }
}



/**
 * Checks water level condition using external float switch to determine whether pump can be run or not
 */
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
  time_over_check = millis();

  if(conf.relay == 1)
  {
    max_runtime = conf.relay_runtime * 1000;
    timeover = ((time_over_check - conf.relay_start) > max_runtime);

    //debugPrint("elapsed runtime time = " + String((time_over_check - conf.relay_start)));
    
    if(!LIQUID_LEVEL_OK || timeover)
    {
      if(PUMP_RUN_REQUEST_TOKEN){
        PUMP_RUN_REQUEST_TOKEN = false;
      }
      
      stopPump();
    }
  }
}



/**
 * Switches off relay(s)
 */
void switchOffCompositeRelay()
{
    debugPrint("Turning off control pins");
    
    conf.relay=0;
    conf.relay_stop = millis();
    digitalWrite(RELAY1, LOW);
    digitalWrite(RELAY2, HIGH);
}



/**
 * Switches on relay(s)
 */
void switchOnCompositeRelay()
{
    debugPrint("Turning on control pins");
  
    conf.relay=1;
    conf.relay_start = millis();    
    digitalWrite(RELAY1, HIGH);
    digitalWrite(RELAY2, LOW);
}



/**
 * Checks composite relay state by reading the control pin(s). Both relays are required to switch the device on.
 */
boolean isCompositeRelayOn()
{
  int relay_1_state = digitalRead(RELAY1_READER); // low by default => off
  int relay_2_state = digitalRead(RELAY2_READER); // high by default => off

  PIN_ERROR = false; // assume no pin error till we evaluate

  if(relay_1_state == 1 && relay_2_state == 0)
  {
    return true;
  }
  else
  {
    if((relay_1_state == 0 && relay_2_state == 0) || (relay_1_state == 1 && relay_2_state == 1)){
      PIN_ERROR = true;
    }
    
    return false;
  }
}



/**
 * Check the running status of the pump by reading the acknowledgement pin
 */
boolean isPumpRunning()
{
  int sensor_state = digitalRead(PUMP_SENSOR);
  if(sensor_state == HIGH){
    return true;
  }else{
    return false;
  }
}



/*
 *Simulates pump running after a few seconds of relay start 
 */
boolean isPumpRunningSim()
{
  boolean result = false;
  
  if(conf.relay == 1)
  {
    if(millis() - conf.relay_start > 3000)
    {
      result = true;
    }
  }
  else
  {
    result = false;
  }
  
  return result;
}




/**
 * Setup
 */
void setup() {

  Serial.begin(9600); 
  queue.setPrinter (Serial); 
  
  // Check for reset and do reset routine
  readSettings();  
  if(conf.reset == 1){
    debugPrint("Reset flag detected!");    
    doReset();
  }

  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);  

  ledOn();
  delay(INIT_DELAY);
  ledOff();

  pinMode(BEEPER, OUTPUT);
  digitalWrite(BEEPER, LOW);

  pinMode(RELAY1, OUTPUT); 
  digitalWrite(RELAY1, LOW);
  
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY2, HIGH);

  pinMode(RELAY1_READER, INPUT);
  pinMode(RELAY2_READER, INPUT);
  pinMode(LIQUID_LEVEL_SENSOR, INPUT);

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
  server->on("/notify", getNotify);
  server->on("/notify/set", setNotify);
  server->on("/notify/url", getNotifyURL);
  server->on("/notify/url/set", setNotifyURL);
  
  server->onNotFound(handleNotFound);
  server->begin();
  
  debugPrint("HTTP server started");
  debugPrint(String(WiFi.localIP()));
}




/**
 * Main loop
 */
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
      checkPumpRunningStatus();      
      relayConditionSafeGuard();
      preventUnauthorizedRun();
      beepOnFault();

      dispatchPendingNotification();
      
      delay(3);

      if(millis() - system_start_time > wait_time)
      {
        server->handleClient();
      }
    }
}


/**
 * Resets the state of the device by resetting configuration data and erasing eeprom
 */
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




/**
 * Initializing
 */
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




/**
 * Erases eeprom of all settings
 */
void eraseSettings()
{
  EEPROM.begin(512);
  
  debugPrint("Erasing eeprom...");
  
  for (int i = 0; i < 512; i++){
    EEPROM.write(i, 0);
  }

  EEPROM.commit();
  EEPROM.end();
}




/**
 * Writes active configuration  to eeprom
 */
void writeSettings() 
{
  EEPROM.begin(512);
  
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

  EEPROM.end();
  
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


/**
 * Write string to eeprom
 */
void writeEEPROM(int startAdr, int len, char* writeString) {
  //yield();
  for (int i = 0; i < len; i++) {
    EEPROM.write(startAdr + i, writeString[i]);
  }
}




/**
 * Reads last written configuration object from eeprom
 */
void readSettings() 
{
  EEPROM.begin(512);
  
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

  EEPROM.end();

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



/**
 * Reads string from eeprom
 */
void readEEPROM(int startAdr, int maxLength, char* dest) {

  for (int i = 0; i < maxLength; i++) {
    dest[i] = char(EEPROM.read(startAdr + i));
  }
}




/**
 * Prints message to serial
 */
void debugPrint(String message){
  if(debug){
    Serial.println(message);
  }
}


