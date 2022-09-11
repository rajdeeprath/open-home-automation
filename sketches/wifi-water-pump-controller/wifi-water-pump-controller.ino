#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <QueueArray.h>
#include <ArduinoLog.h>
#include <WiFiClient.h>

#define RELAY1 4 
#define RELAY2 5 
#define RELAY1_READER 12
#define RELAY2_READER 14
#define LIQUID_LEVEL_SENSOR A0 
#define LED 16 
#define BEEPER 13 
#define PUMP_SENSOR 15

#define NOTICE_LIMIT 5

const char* NAME="HMU-PC-001";
const char* AP_PASS="iot@123!";

double digital_adc_voltage;
float OP_VOLTAGE = 3.3;
float MAX_VOLTAGE = 1023.0;
long current_time;
long lastOpenSwitchDetect;
long timeSinceLastOpenSwitch;
long adcLastReadTime;
boolean switchOpen;
boolean _connected = false;
boolean _connecting = false;
boolean webserver_inited = false;

int DEFAULT_RUNTIME = 30;
long max_runtime;
long system_start_time;
long wait_time = 5000;
boolean debug=true;
boolean inited = false;
boolean timeover;
int eeAddress = 0;
int liquidLevelSensorReadIn = 0;
int liquidLevelSensorReadInThreshold = 900;
String switch1state;
boolean LIQUID_LEVEL_OK = true;
boolean PUMP_CONNECTION_ON = false;
boolean PUMP_RUN_REQUEST_TOKEN = false;
String data;

long last_notify = 0;
long accidentGuardLastRun = 0;
boolean PIN_ERROR = false;
long time_over_check;
long lastBeepStateChange;
boolean systemFault;
boolean beeping;

long lastPumpStartFeedbackTime;
long lastPumpStopFeedbackTime;

long INIT_DELAY = 15000;
long CONSECUTIVE_NOTIFICATION_DELAY = 5000;
long CONSECUTIVE_PUMP_RUN_DELAY = 15000;
long OPERATION_FEEDBACK_CHECK_DELAY = 2000;
long START_REQUEST_TOKEN_INVALIDATE_TIME = 10000;
long ACCIDENT_GUARD_RUN_DELAY = 4000;
long BEEPER_DELAY = 1500;
long PUMP_CONNECTION_SENSOR_NOISE_THRESHOLD = 2000;

String CAPABILITIES = "{\"name\":\"" + String(NAME) + "\",\"devices\":{\"name\":\"Irrigation Pump Controller\",\"actions\":{\"getSwitch\":{\"method\":\"get\",\"path\":\"\/switch\/1\"},\"toggleSwitch\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\"},\"setSwitchOn\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\/on\"},\"setSwitchOff\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\/off\"}, \"getRuntime\":{\"method\":\"get\",\"path\":\"\/switch\/1\/runtime\"},\"setRuntime\":{\"method\":\"get\",\"path\":\"\/switch\/1\/runtime\",\"params\":[{\"name\":\"time\",\"type\":\"Number\",\"values\":\"60, 80, 100 etc\"}]}}},\"global\":{\"actions\":{\"getNotify\":{\"method\":\"get\",\"path\":\"\/notify\"},\"setNotify\":{\"method\":\"get\",\"path\":\"\/notify\/set\",\"params\":[{\"name\":\"notify\",\"type\":\"Number\",\"values\":\"1 or 0\"}]},\"getNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\"},\"setNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\/set\",\"params\":[{\"name\":\"url\",\"type\":\"String\",\"values\":\"http:\/\/google.com\"}]},\"reset\":\"\/reset\",\"info\":\"\/\"}}}";

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
WiFiEventHandler gotIpEventHandler, disconnectedEventHandler;
std::unique_ptr<ESP8266WebServer> server;

HTTPClient http;
WiFiClient wifiClient;

boolean posting;




/**
 * Gets called when config portal is running
 */

void configModeCallback (WiFiManager *myWiFiManager) 
{
  Log.trace("Inside configModeCallback" CR);
  Log.notice("IP address: : %d.%d.%d.%d" CR, WiFi.softAPIP()[0],  WiFi.softAPIP()[1], WiFi.softAPIP()[2], WiFi.softAPIP()[3]);
  Log.notice("ConfigPortalSSID => %s" CR, myWiFiManager->getConfigPortalSSID());
}




/**
 * Handle root visit
 */
void handleRoot() {
  Log.trace("handleRoot called" CR);
  server->send(200, "application/json", CAPABILITIES);
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
  Log.trace("Preparing notification" CR);
  
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

   if(queue.count() < NOTICE_LIMIT){
    Log.trace("Pushing notification to queue" CR);
    queue.enqueue(notice);
   }
}



String getPostNotificationString(Notification &notice)
{
      String post = "";
      post += "pump=" + String(notice.relay);
      post += "&run_time" + String(notice.relay_runtime); 
      post += "&relay_start" + String(notice.relay_start); 
      post += "&relay_stop" + String(notice.relay_stop); 
      post += "&queue_time=" + String(notice.queue_time); 
      post += "&send_time=" + String(notice.send_time); 
      post += "&message=" + String(notice.message);
      return post;
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
      Log.trace("Running Notification service" CR);
      Log.trace("Popping notification from queue. Current size = %d" CR, queue.count());
      
      Notification notice = queue.dequeue();
      notice.send_time = millis();  
      posting = true;
        
      data = getPostNotificationString(notice);

      http.begin(wifiClient, conf.endpoint);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      http.addHeader("Host", "iot.flashvisions.com");
      http.addHeader("Content-Length", String(data.length()));
      int httpResponseCode = http.POST(data); 
      Log.notice("HTTP Response code: %d" CR, httpResponseCode);
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
  const char* msg;
  
   if(conf.relay == 0)
   {
      if(millis() - conf.relay_start > CONSECUTIVE_PUMP_RUN_DELAY)
      {
        Log.trace("Starting pump!" CR);
        switchOnCompositeRelay();
      }
      else
      {
        msg = "Pump was last run very recently. It cannot be run consecutively. Try after some time!";
        Log.trace(msg CR);
        notifyURL(msg);
      }
   }
   else
   {
        msg = "Pump cannot be started now as it was already started or is still running and has not stopped automatically!";
        Log.trace(msg CR);
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
    Log.trace("Stopping pump!" CR);
    switchOffCompositeRelay();
  }
}



/**
 * Prevents accidental runs of the pump controller under unexpected component failures or runaways logic.
 */
void checkUnauthorizedRun()
{
    const char* msg;
    boolean RED_FLAG;
    long lastRunElapsedTime = (millis() - conf.relay_start);
    long lastStopElapsedTime = (millis() - conf.relay_stop);
    boolean canCheck = ((lastRunElapsedTime > OPERATION_FEEDBACK_CHECK_DELAY) && (lastStopElapsedTime > OPERATION_FEEDBACK_CHECK_DELAY) && (millis() - accidentGuardLastRun > ACCIDENT_GUARD_RUN_DELAY));

    if(canCheck)
    {
        /* Check for possible fault */    
        if(!PUMP_RUN_REQUEST_TOKEN)
        {
          //Log.trace("Running accident prevention & warning routine checks!!");         
          
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
          
          Log.trace(msg CR);
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

    const char* msg;
    
    if(!isPumpRunning())
    {
      if(PUMP_CONNECTION_ON)
      {
        PUMP_CONNECTION_ON = false;
        lastPumpStopFeedbackTime = millis();
                
        msg = "Pump stopped!";
        Log.trace(msg CR);
        
        // notify status
        notifyURL(msg);
      }
    }
    else
    {
      if(!PUMP_CONNECTION_ON)
      {   
        // Check for sensor noise / bad behaviour
        if(millis() - lastPumpStopFeedbackTime > PUMP_CONNECTION_SENSOR_NOISE_THRESHOLD)     
        {
          PUMP_CONNECTION_ON = true;
          lastPumpStartFeedbackTime = millis();
        
          msg = "Pump running!";
          Log.trace(msg CR);

          // notify status
          notifyURL(msg);
        }        
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
void handleSystemFault()
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
  current_time = millis();

  if(current_time - adcLastReadTime < 100){
    return;
  }
  
  // read in liquid level from ADC
  liquidLevelSensorReadIn = analogRead(LIQUID_LEVEL_SENSOR);
  adcLastReadTime = current_time;

  // calculate digital voltage
  digital_adc_voltage = liquidLevelSensorReadIn * (OP_VOLTAGE / MAX_VOLTAGE);  
  //Log.trace("Analog Voltage = %d, Digital Voltage = %D" CR, liquidLevelSensorReadIn, digital_adc_voltage);  

  // if digital voltage greater than 2.7 take action ->
  if(digital_adc_voltage >= 2.7)
  {
    // if switch was not open then it is opened now
    if(!switchOpen)
    {
      switchOpen = true; // mark switch as open
      lastOpenSwitchDetect = current_time; // remember the time when it was opened
      Log.trace("switch open" CR);   
    }
  }
  else // if digital voltage is less then 2.7 take action ->
  {
    // if switch was open then mark it as closed now
    if(switchOpen)
    {
      switchOpen = false; // mark switch as closed
      lastOpenSwitchDetect = current_time + 1000; // forward last open time into future to fool `timeSinceLastOpenSwitch`
      Log.trace("switch closed" CR);
    }
  }


  timeSinceLastOpenSwitch = current_time - lastOpenSwitchDetect; // how long was it since we have a open switch
  //Log.trace("timeSinceLastOpenSwitch = %l" + timeSinceLastOpenSwitch);  
  
  if(switchOpen && timeSinceLastOpenSwitch >= 500) // if switch is open and it has been that way for more then 500m ms (normalising false values)
  {
    // if liquid level was ok, now mark it as not ok
    if(LIQUID_LEVEL_OK)
    {
      ledOn(); // turn on indicator
      LIQUID_LEVEL_OK = false; // liquid level not ok
    } 
  }
  else // if switch is closed
  {
    // if liquid level was not ok, now mark it as ok
    if(!LIQUID_LEVEL_OK)
    {
      ledOff(); // turn off indicator
      LIQUID_LEVEL_OK = true; // liquid level ok
    }
  }


  /****************************************************************/

  // if relay was on check if it should be autoclosed now
  if(conf.relay == 1)
  {
    max_runtime = conf.relay_runtime * 1000; // calculate max runtime in ms
    timeover = ((current_time - conf.relay_start) > max_runtime); // check time elapsed since pump relay turned on

    //Log.trace("elapsed runtime time = " + String((time_over_check - conf.relay_start)));

    // if liquid level not ok or maxrelay runtime is over -> turn off relay now
    if(!LIQUID_LEVEL_OK || timeover)
    {      
      stopPump();
    }
  }
}



/**
 * Switches off relay(s)
 */
void switchOffCompositeRelay()
{
    Log.trace("Turning off control pins" CR);
    
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
    Log.trace("Turning on control pins" CR);
  
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
    if(millis() - conf.relay_start > 1000)
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
  Log.begin(LOG_LEVEL_TRACE, &Serial);  
  Log.notice("Serial initialize!" CR);  
  
  queue.setPrinter (Serial); 
  
  // Check for reset and do reset routine
  readSettings();  
  if(conf.reset == 1){
    Log.trace("Reset flag detected!" CR);
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


  /* WIFI */

  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP   
    
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setConfigPortalBlocking(false);


  gotIpEventHandler = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP & event)
  {
    _connected = true;
    Log.notice("Network connected ");
    Log.notice("IP address: : %d.%d.%d.%d" CR, WiFi.localIP()[0],  WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);

    // Start webserver
    if(!webserver_inited){
      init_server();
    }
  });
  
  
  disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected & event)
  {
    _connected = false;
    Log.notice("Network disconnected!" CR);

    if(WiFi.status()== WL_CONNECTED)
    {
      Log.notice("WL_CONNECTED" CR);
    }
    else
    {
      Log.notice("WL_DISCONNECTED" CR);
    }
    
  });

  
  connect_network();
}




/**
 * Attempt to connect to WIFI network
 */
void connect_network()
{
  Log.notice("Attempting to connect to network using saved credentials" CR);  
  Log.notice("Connecting to WiFi..");
  
  _connecting = true;
  
  boolean wm_connected = wifiManager.autoConnect(NAME, AP_PASS);
  if(wm_connected)
  {
    Log.notice("connected...yeey :)");
    Log.notice("IP address: : %d.%d.%d.%d" CR, WiFi.localIP()[0],  WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);

    _connecting = false;
    _connected = true;
  }else
  {
    Log.notice("Config portal running");
    _connecting = false;
    _connected = false;
  }
}




/**
 * Initialize webserver @ port 80
 */
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
  
  Log.notice("HTTP server started!" CR);  
  webserver_inited = true;
}




/**
 * Main loop
 */
void loop() 
{
    wifiManager.process();
    
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
      
      checkPumpRunningStatus();
      checkUnauthorizedRun();
      handleSystemFault();
      
      dispatchPendingNotification();
      invalidateUserRequest();  

      if((millis() - system_start_time > wait_time) && (WiFi.status()== WL_CONNECTED))
      {
        server->handleClient();
      }
    }
}



/* Invalidate user request */
void invalidateUserRequest()
{
  long lastRunElapsedTime = (millis() - conf.relay_start);
  long lastStopElapsedTime = (millis() - conf.relay_stop);
  boolean canReleaseToken = ((lastStopElapsedTime > START_REQUEST_TOKEN_INVALIDATE_TIME) && (conf.relay_stop > conf.relay_start) && conf.relay_start > 0 && conf.relay_stop > 0);
  
  if(canReleaseToken)
  {
        if(PUMP_RUN_REQUEST_TOKEN)  
        {
          PUMP_RUN_REQUEST_TOKEN = false;
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
}




/**
 * Initializing
 */
void initSettings()
{
  readSettings();

  if(conf.relay_runtime <= 0)
  {
    Log.trace("Setting defaults" CR);
    
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



void set_defaults()
{  
  Log.trace("Setting defaults" CR);
  
  String url = "0.0.0.0";
  char tmp[url.length() + 1];
  url.toCharArray(tmp, url.length() + 1);

  conf.endpoint_length = url.length();
  memset(conf.endpoint, 0, sizeof(conf.endpoint));
  strncpy(conf.endpoint, tmp, strlen(tmp));
  
  conf.notify = 0;
  conf.relay_runtime = DEFAULT_RUNTIME;

  system_start_time = millis();
  conf.relay=0;
  conf.led=1;
  conf.lastupdate = 0;
  conf.relay_start = 0;
  conf.relay_stop = 0;

  writeSettings();
}




/**
 * Erases eeprom of all settings
 */
void eraseSettings()
{
  EEPROM.begin(512);
  
  Log.trace("Erasing eeprom..." CR);
  
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
  
  Log.trace("Conf saved" CR);
  Log.trace("relay => %d" CR, conf.relay);
  Log.trace("relay_runtime => %d" CR, conf.relay_runtime);
  Log.trace("led => %d" CR, conf.led);
  Log.trace("lastupdate => %l" CR, conf.lastupdate);
  Log.trace("relay_start => %l" CR, conf.relay_start);
  Log.trace("relay_stop=> %l" CR, conf.relay_stop);
  Log.trace("reset => %d" CR, conf.reset);
  Log.trace("notify => %d" CR, conf.notify);
  Log.trace("endpoint_length => %d" CR, conf.endpoint_length);
  Log.trace("endpoint_length => %s" CR, conf.endpoint);
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

  Log.trace("Conf read" CR);
  Log.trace("relay => %d" CR, conf.relay);
  Log.trace("relay_runtime => %d" CR, conf.relay_runtime);
  Log.trace("led => %d" CR, conf.led);
  Log.trace("lastupdate => %l" CR, conf.lastupdate);
  Log.trace("relay_start => %l" CR, conf.relay_start);
  Log.trace("relay_stop=> %l" CR, conf.relay_stop);
  Log.trace("reset => %d" CR, conf.reset);
  Log.trace("notify => %d" CR, conf.notify);
  Log.trace("endpoint_length => %d" CR, conf.endpoint_length);
  Log.trace("endpoint_length => %s" CR, conf.endpoint);
}



/**
 * Reads string from eeprom
 */
void readEEPROM(int startAdr, int maxLength, char* dest) {

  for (int i = 0; i < maxLength; i++) {
    dest[i] = char(EEPROM.read(startAdr + i));
  }
}
