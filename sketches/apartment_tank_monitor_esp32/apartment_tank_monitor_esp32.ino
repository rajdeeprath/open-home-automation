#include <TaskScheduler.h>

#include <Wire.h>
#include <RTClib.h> // https://github.com/adafruit/RTClib
#include <LiquidCrystal_I2C.h> // https://github.com/fdebrabander/Arduino-LiquidCrystal-I2C-library

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <sys/time.h>

#include <QueueArray.h>
#include <ArduinoLog.h>

#define SENSOR_1_LEVEL 4
#define SENSOR_1_DATA 34

// top sensor
#define SENSOR_2_LEVEL 16
#define SENSOR_2_DATA 35

// middle sensor
#define SENSOR_3_LEVEL 17 //level - blue | black
#define SENSOR_3_DATA 36 //data - bluewhite | yellow

// bottom sensor
#define SENSOR_4_LEVEL 18 //level - greenwhite/white | black
#define SENSOR_4_DATA 39 //data - green | yellow

// indicators
#define ALARM 19 //32
#define LED_SYSTEM 33
#define LED_PUMP 13
#define LED_HIGH 25
#define LED_MID 26
#define LED_LOW 27

#define BEEPER 23
#define NOTICE_LIMIT 5

const String NAME="AMU-PC-001";

String data;

boolean PUMP_EVENT = false;
boolean EMERGENCY_PUMP_EVENT = false;
boolean POWER_SAVER = false;
boolean MAINTAINENCE_MODE = false;
boolean SOFTRESET = true;

long last_notify = 0;
long lastBeepStateChange;
long lastPumpLedUpdate;
long lastSystemLedUpdate;
long lastAlarmUpdate;
long overFlowAlarmStart;

long currentTimeStamp;

boolean systemFault;
boolean beeping;

boolean debug = false;
int echo = 1;
int error = 0;

String subMessage;

const long OVERFLOW_ALARM_TIME_THRESHOLD = 60000;
const long CONSECUTIVE_NOTIFICATION_DELAY = 5000;
const long SENSOR_STATE_CHANGE_THRESHOLD = 60000;
const long PUMP_SENSOR_STATE_CHANGE_THRESHOLD = 10000;
const long OVERFLOW_STATE_THRESHOLD = 60000;
const long SENSOR_TEST_THRESHOLD = 120000;
const long INDICATOR_TEST_THRESHOLD = 120000;
boolean TIME_SYNCED = false;

const char* ssid = "Tomato24";
const char* password =  "bhagawadgita@123";


struct Settings {
   long lastupdate;
   int reset = 0;
   int notify = 1;
   int endpoint_length;
   char endpoint[80] = "";
};


struct TankState {
   int low = 0;
   int mid = 0;
   int high = 0;
   int pump = 0;
   long lastupdate = 0;
};


struct SensorState {
   int low = 0;
   int mid = 0;
   int high = 0;
   int pump = 0;
   long lastupdate = 0;
};


struct IndicatorState {
   int low = 0;
   int mid = 0;
   int high = 0;
   int pump = 0;
   int sys = 0;
   int alarm = 0;
   int beeper = 0;
   long lastupdate = 0;
};


struct Notification {
   int low;
   int mid;
   int high;
   int pump;
   float temperature = 0.0;
   int health;
   int echo;
   char message[80] = "";
   long queue_time;
   long send_time;
   int days_running;
   int error=0;
   int debug=0;
   char clocktime[100] = "";
};


QueueArray <Notification> queue;
Settings conf = {};
TankState tankState = {};
SensorState sensors = {};
IndicatorState indicators = {};

boolean posting;
boolean stateChanged = false;


float temperature = 0.0;
boolean inited = false;
long initialReadTime = 0;
long minInitialSensorReadTime = 15000;
long minSensorTestReadtime = 15000;
long minIndicatorTestReadtime = 10000;
long minHardwareInitializeTime = 20000;

boolean sensorCheck = false;
boolean indicatorCheck = false;
long sensorTestTime = 0;
long indicatorTestTime = 0;
boolean sensorsInvert = false;

int low, mid, high, pump;
long lastLowChange, lastMidChange, lastHighChange, lastPumpChange, lastOverflowCondition;
int normalLow, normalMid, normalHigh, normalPump;
int invertLow, invertMid, invertHigh, invertPump;

int health = 1;
boolean SENSOR_TEST_EVENT = false;
boolean INDICATOR_TEST_EVENT = false;
boolean RESET_EVENT = false;
int INSUFFICIENTWATER = 0;
int SYSTEM_ERROR = 0;
long lastSensorTest = 0;
long lastIndicatorTest = 0;
boolean forcePumpOn = false;
boolean isPumpSensorNpN = true;
int daysRunning = 0;
int MAX_DAYS_RUNNING = 3;
int lastRunDay = 0;

WiFiClient client;
HTTPClient http;
RTC_DS3231 rtc;
DateTime rtctime;

LiquidCrystal_I2C lcd(0x3F, 16, 2);


uint64_t chipid;  
struct tm timeinfo;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800;
const int   daylightOffset_sec = 0;

const char* serverName = "http://iot.flashvisions.com/?amu_pc_001=1";


// methods prototypes
void dispatchPendingNotification();



// Tasks
Task notification_task(1000, TASK_FOREVER, &dispatchPendingNotification);


// Scheduler
Scheduler runner;



void doReset(){
 ESP.restart();
}



void lcd_print(char* line, int posx=0, int posy=0, bool backlit=true, bool clean=true)
{
  if(clean){
    lcd.clear();
  }

  if(backlit){
    lcd.backlight(); 
  }else{
    lcd.noBacklight();
  }
  
  lcd.setCursor(posx, posy);
  lcd.print(line);
}



void lcd_print_sensors(bool backlit=true)
{
  char msg[30];
  sprintf(msg, "Sensors: %d|%d|%d|%d", tankState.pump, tankState.high, tankState.mid, tankState.low);
  lcd_print(msg, 0, 0, backlit);
}


void lcd_print_rtc_time(bool backlit=true)
{
  rtctime = rtc.now();
        
  char time_str[20];
  sprintf(time_str, "%d/%d/%d %d:%d", rtctime.day(), rtctime.month(), rtctime.year(), rtctime.hour(), rtctime.minute());        
  lcd_print(time_str, 0, 1, backlit, false);
}


void lcd_print_system_time(bool backlit=true)
{
  if(!getLocalTime(&timeinfo)){
    Log.error("Failed to obtain time" CR);
    lcd_print("Failed to obtain time", 0, 1, backlit, false);
    return;
  }

  char buff[20];
  sprintf(buff, "%d/%d/%d %d:%d", timeinfo.tm_mday, timeinfo.tm_mon, timeinfo.tm_year, timeinfo.tm_hour, timeinfo.tm_min);        
  lcd_print(buff, 0, 1, backlit, false);
}



void printLocalTime()
{  
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}


void setup() {
    
    Serial.begin(115200);
    Log.begin(LOG_LEVEL_NOTICE, &Serial);
    
    Log.notice("Preparing to start" CR);


    /* INIT PINS */   

    // pump sensor
    pinMode(SENSOR_1_LEVEL, OUTPUT);//level
    pinMode(SENSOR_1_DATA, INPUT);//data  
  
    // top sensor
    pinMode(SENSOR_2_LEVEL, OUTPUT);//level
    pinMode(SENSOR_2_DATA, INPUT);//data  
  
    // middle sensor
    pinMode(SENSOR_3_LEVEL, OUTPUT);//level
    pinMode(SENSOR_3_DATA, INPUT);//data  
  
    // bottom sensor
    pinMode(SENSOR_4_LEVEL, OUTPUT);//level
    pinMode(SENSOR_4_DATA, INPUT);//data


    // init indicators

    pinMode(LED_LOW, OUTPUT);
    digitalWrite(LED_LOW, LOW);
    
    pinMode(LED_MID, OUTPUT);
    digitalWrite(LED_MID, LOW);
    
    pinMode(LED_HIGH, OUTPUT);
    digitalWrite(LED_HIGH, LOW);
    
    pinMode(LED_SYSTEM, OUTPUT);
    digitalWrite(LED_SYSTEM, LOW);
    
    pinMode(LED_PUMP, OUTPUT);
    digitalWrite(LED_PUMP, LOW);

    pinMode(ALARM, OUTPUT);
    digitalWrite(ALARM, LOW);

    pinMode(BEEPER, OUTPUT);
    digitalWrite(BEEPER, LOW);
    
    

    /* Init LCD */
    
    lcd.init();
    lcd_print("  INITIALIZING ", 0, 0);
    delay(2000);



    /* INIT RTC */
    
    if (!rtc.begin()) 
    {
      Log.notice("RTC Failure" CR);
      lcd_print(" RTC Failure ", 0, 0); 
      beeperOn();
            
      while(true){ delay(100);}
    }
    else
    {
      if (rtc.lostPower()) 
      {
        Serial.println("RTC lost power, let's set the time!");
        rtc.adjust(DateTime(__DATE__, __TIME__));
      }
      else
      {
        rtctime = rtc.now();
        
        lcd_print_rtc_time();
        delay(2000);

        struct timeval tv;
        tv.tv_sec=rtctime.unixtime() - 19800;

        settimeofday(&tv, NULL);
        setenv("TZ", "IST-5:30", 1);
        tzset();

        TIME_SYNCED = true;
        
      }
    } 


    /* GET CHIPID */
    
    chipid=ESP.getEfuseMac();//The chip ID is essentially its MAC address(length: 6 bytes).
    char chipid_str[20];
    sprintf(chipid_str, "ID: %04X%08X", (uint16_t)(chipid>>32), (uint32_t)chipid);
    lcd_print(chipid_str, 0, 0, false);    
    delay(2000);
    

    /* INIT WIFI */

    Log.notice("Attempting to connect to network using saved credentials" CR);  
    WiFi.begin(ssid, password); 
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.println("Connecting to WiFi..");
      lcd_print(" Connecting... ", 0, 0);
    }
   
    lcd_print(" WIFI CONNECTED ", 0, 0);
    delay(2000);

    if(!TIME_SYNCED)
    {
      setTimeByNTP();
      TIME_SYNCED = true;          
    }


    /* Print synced system time */
    printLocalTime();
    

    /* Misc init */  
    initialReadTime = millis();
    lcd_print(" I AM  READY   ", 0, 0, false);    
    lcd_print_sensors(false);   

    
    /* Prepare scheduler */

    runner.init();
    Log.notice("Initialized scheduler" CR);
    
    runner.addTask(notification_task);
    Serial.println("added notification_task");


    /* Notify start */
    
    notifyURL("System Reset!", 0, 1);
}




/**
 * Reads time from internet and syncs system and RTC clock to it
 */
void setTimeByNTP() {
  struct tm t;

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  if (!getLocalTime(&t)) {
    Serial.println("getLocalTime Error");
    return;
  }
  
  //rtc.adjust(DateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec));
}



void beeperOn()
{
  if(indicators.beeper == 0)
  {
    digitalWrite(BEEPER, HIGH);
    indicators.beeper = 1;
  }
}


void beeperOff()
{
  if(indicators.beeper == 1)
  {
    digitalWrite(BEEPER, LOW);
    indicators.beeper = 0;
  }
}



void lowLedOn()
{
  if(indicators.low == 0)
  {
    digitalWrite(LED_LOW, HIGH);
    indicators.low = 1;
  }
}


void lowLedOff()
{
  if(indicators.low == 1)
  {
    digitalWrite(LED_LOW, LOW);
    indicators.low = 0;
  }
}



void midLedOn()
{
  if(indicators.mid == 0)
  {
    digitalWrite(LED_MID, HIGH);
    indicators.mid = 1;
  }
}


void midLedOff()
{
  if(indicators.mid == 1)
  {
    digitalWrite(LED_MID, LOW);
    indicators.mid = 0;
  }
}




void highLedOn()
{
  if(indicators.high == 0)
  {
    digitalWrite(LED_HIGH, HIGH);
    indicators.high = 1;
  }
}


void highLedOff()
{
  if(indicators.high == 1)
  {
    digitalWrite(LED_HIGH, LOW);
    indicators.high = 0;
  }
}


void pumpLedOn()
{
  if(indicators.pump == 0)
  {
    digitalWrite(LED_PUMP, HIGH);
    indicators.pump = 1;
  }
}


void pumpLedOff()
{
  if(indicators.pump == 1)
  {
    digitalWrite(LED_PUMP, LOW);
    indicators.pump = 0;
  }
}



void blinkPumpLed()
{
  currentTimeStamp = millis();
  
  if(currentTimeStamp - lastPumpLedUpdate > 2000)
  {
    if(indicators.pump == 1)
    {
      pumpLedOff();
    }
    else if(indicators.pump == 0)
    {
      pumpLedOn();
    }
    lastPumpLedUpdate = currentTimeStamp;
  }
}



void blinkAlarm()
{
  currentTimeStamp = millis();
  
  if(currentTimeStamp - lastAlarmUpdate > 600)
  {
    if(indicators.alarm == 1)
    {
      alarmOff();
    }
    else if(indicators.alarm == 0)
    {
      alarmOn();
    }
    lastAlarmUpdate = currentTimeStamp;
  }
}


/*
void blinkSystemLed()
{
  currentTimeStamp = millis();
  
  if(currentTimeStamp - lastSystemLedUpdate > 2000)
  {
    if(indicators.sys == 1)
    {
      systemLedOff();
    }
    else if(indicators.sys == 0)
    {
      systemLedOn();
    }
    lastSystemLedUpdate = currentTimeStamp;
  }
}
*/


void systemLedOn()
{
  if(indicators.sys == 0)
  {
    digitalWrite(LED_SYSTEM, HIGH);
    indicators.sys = 1;
  }
}


void systemLedOff()
{
  if(indicators.sys == 1)
  {
    digitalWrite(LED_SYSTEM, LOW);
    indicators.sys = 0;
  }
}




void alarmOn()
{
  if(indicators.alarm == 0)
  {
    digitalWrite(ALARM, HIGH);
    indicators.alarm = 1;
  }
}


void alarmOff()
{
  if(indicators.alarm == 1)
  {
    digitalWrite(ALARM, LOW);
    indicators.alarm = 0;
  }
}



void allIndicatorsOn()
{
  lowLedOn();
  midLedOn();
  highLedOn();
  pumpLedOn();
  systemLedOn();
  blinkAlarm();
}


void allIndicatorsOff()
{
  lowLedOff();
  midLedOff();
  highLedOff();
  pumpLedOff();
  systemLedOff();
  alarmOff();
}



/* First read of sensors as soon as system starts */
void initSensors()
{
  // indicate system startup
  systemLedOn();
  
  // initially we read in all sensors  
  
  // read bottom sensor
  tankState.low = readSensor(SENSOR_4_DATA);

  // read middle sensot
  tankState.mid = readSensor(SENSOR_3_DATA);

  // read top sensot
  tankState.high = readSensor(SENSOR_2_DATA);

  // read pump sensor
  tankState.pump = readSensor(SENSOR_1_DATA);

  char msg[30];
  sprintf(msg, "Sensors : %d | %d | %d | %d", tankState.pump, tankState.high, tankState.mid, tankState.low);
  Log.trace("Sensors : %s" CR, msg);
  
  // initial read time
  
  if(millis() - initialReadTime > minInitialSensorReadTime)
  {
      inited = true;
      Log.notice("Inited : " CR);
      systemLedOff();

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
  else if(tankState.mid == 1)
  {
    message = "Water Level between 50% to 100%";
  }
  else if(tankState.low == 1)
  {
    message = "Water Level between 10% to 50%";
  }
  else
  {
    message = "Water Level Critical! (less than 10%)";
  }

  return message;
}



int readSensor(int pin)
{
  // for npn sensor invert reading to match our system requirements
  if(pin == SENSOR_1_DATA && isPumpSensorNpN == true)
  {
    int pump = digitalRead(pin);
    
    if(pump == 1)
    {
      pump = 0;
    }
    else if(pump == 0)
    {
      pump = 1;
    }

    return pump;
  }
  else
  {
    return digitalRead(pin);
  }
}


/**
 * Evaluates sensor power state. Method considers 'power saver mode' to intelligently turn sensor on/off to extend life and save more power
 **/ 
void evaluateTankState()
{
    if(!inited){
      return;
    }

    subMessage = "";
    stateChanged = false;
    error = 0;
    
    // read sensor data
    low = readSensor(SENSOR_4_DATA);
    mid = readSensor(SENSOR_3_DATA);
    high = readSensor(SENSOR_2_DATA);

    // special condition handling
    if(forcePumpOn)
    {
      if(PUMP_EVENT)
      {
        pump = 1;
      }
      else
      {
        pump = 0;
      }
    }
    else
    {
      pump = readSensor(SENSOR_1_DATA);
    } 

    Log.trace("=======================================================================" CR);
    Log.trace("Sensors : %d | %d | %d | %d" CR, pump, high, mid, low);
    
    lcd_print_sensors();


    // update low level state
    if(hasLowChanged())
    {
      if(low == 1)
      {
        if(pump == 1)
        {
          subMessage = "Water Level risen to 10% ";
        }
        else if(forcePumpOn)
        {
          subMessage = "Water Level risen to 10% (Emergency pump run)";
          EMERGENCY_PUMP_EVENT = true;
        }
        else
        {
          error = 1;
          subMessage = "Unexpected state change of 'low' sensor";
        }
      }
      else
      {
        if(mid == 0 && high == 0)
        {
          subMessage = "Water Level dropped below 10% ";
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



    // update mid level state
    if(hasMidChanged())
    {
      if(mid == 1)
      {
        if(low == 1 && pump == 1)
        {
          subMessage = "Water Level risen to 50% ";
        }
        else
        {
          error = 1;
          subMessage = "Unexpected state change of 'mid' sensor";
        }
      }
      else
      {
        if(high == 0)
        {
          subMessage = "Water Level dropped below 50% ";
        }
        else
        {
          error = 1;
          subMessage = "Unexpected state change of 'mid' sensor";
        }
      }

      if(error == 0){
        stateChanged = true;
        tankState.mid = mid;
      }
    }



    // update high level state
    if(hasHighChanged())
    {
      if(high == 1)
      {
        if(low == 1 && mid ==1 && pump ==1)
        {
          subMessage = "Water Level risen to 100% ";
        }
        else
        {
          error = 1;
          subMessage = "Unexpected state change of 'high' sensor";
        }
      }
      else
      {
        subMessage = "Water Level dropped below 100% ";
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
        subMessage = "Pump Started!\n[" + levelInfo + "]";
      }
      else
      {
        subMessage = "Pump Stopped!\n[" + levelInfo + "]";
      }
      
      stateChanged = true;
      tankState.pump = pump;
    }


    // monitor overflow
    trackOverFlow(tankState.pump, tankState.high);

    // monitor undrflow
    trackInsufficientWater(tankState.low, tankState.mid, tankState.high, tankState.pump);

    // track error
    trackSystemError(error);

    // Indicate change
    updateIndicators(tankState.low, tankState.mid, tankState.high, tankState.pump);


    /***************************/ 
    Log.trace("Sensors : %d | %d | %d | %d" CR, tankState.pump, tankState.high, tankState.mid, tankState.low);
    Log.trace("=======================================================================" CR);
    Log.trace("State changed = %T" CR, stateChanged);


    // evaluate and dispatch message
    if(stateChanged)
    {
      lcd_print_sensors();
      
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



void doSensorTest()
{
  systemLedOn();  
  sensorCheck = true;
  uprightSensorLevels();
  
}


/**
 * Set upright logic level signal for all sensors (HIGH)
 */
void uprightSensorLevels()
{
  sensorTestTime = millis();
  sensorsInvert = false;
  
  Log.notice("Starting sensor test" CR);

  digitalWrite(SENSOR_1_LEVEL, HIGH); // level
  digitalWrite(SENSOR_2_LEVEL, HIGH); // level
  digitalWrite(SENSOR_3_LEVEL, HIGH); // level
  digitalWrite(SENSOR_4_LEVEL, HIGH); // level
}



/**
 * Invert logic level signal for all sensors (LOW)
 */
void invertSensorLevels()
{
  sensorTestTime = millis();
  sensorsInvert = true;

  Log.notice("Sensor levels inverted" CR);
      
  digitalWrite(SENSOR_1_LEVEL, LOW); // level
  digitalWrite(SENSOR_2_LEVEL, LOW); // level
  digitalWrite(SENSOR_3_LEVEL, LOW); // level
  digitalWrite(SENSOR_4_LEVEL, LOW); // level
}



/**
 * End / cancel sensor test
 */
void cancelSensorTest()
{
  systemLedOff();
  
  sensorCheck = false;
  sensorTestTime = millis();
  sensorsInvert = false;

  Log.notice("Stopping sensor test" CR);
  
  digitalWrite(SENSOR_1_LEVEL, HIGH); // level
  digitalWrite(SENSOR_2_LEVEL, HIGH); // level
  digitalWrite(SENSOR_3_LEVEL, HIGH); // level
  digitalWrite(SENSOR_4_LEVEL, HIGH); // level
}


/**
 * Test sesnors normally first and then by inverting. Inverted lopic level should produce inverted output
 */
void testSensors()
{
  if(!sensorsInvert)
  {
    Log.notice("Checking normal sensor states" CR);
    
    // read bottom sensor
    normalLow = readSensor(SENSOR_4_DATA);
  
    // read middle sensot
    normalMid = readSensor(SENSOR_3_DATA);
  
    // read top sensot
    normalHigh = readSensor(SENSOR_2_DATA);
  
    // read pump sensor
    normalPump = readSensor(SENSOR_1_DATA);

    Log.notice("Sensors : %d | %d | %d | %d" CR, normalPump, normalHigh, normalMid, normalLow);
  
    // change condition after minSensorTestReadtime seconds
    if(millis() - sensorTestTime > minSensorTestReadtime){ 
      invertSensorLevels();      
    }
  }
  else 
  {
    Log.notice("Checking invert sensor states" CR);
    
    // read bottom sensor
    invertLow = readSensor(SENSOR_4_DATA);
  
    // read middle sensot
    invertMid = readSensor(SENSOR_3_DATA);
  
    // read top sensor
    invertHigh = readSensor(SENSOR_2_DATA);
  
    // read pump sensor
    invertPump = readSensor(SENSOR_1_DATA);

    Log.notice("Sensors : %d | %d | %d | %d" CR, invertPump, invertHigh, invertMid, invertLow);
    
    // change condition after minSensorTestReadtime seconds
    
    if(millis() - sensorTestTime > minSensorTestReadtime)
    {
      // cancel test
      cancelSensorTest();  

      // record last test time
      lastSensorTest = millis();

      // evaluate result
      if(normalLow != invertLow && normalMid != invertMid && normalHigh != invertHigh && normalPump != invertPump)
      {
        health = 1;
        //forcePumpOn = false;
      }
      else
      {
        String sensorReport = "Sensors problem detected!";
        sensorReport = sensorReport + "\n\r";
        sensorReport = sensorReport + "NL="+normalLow+",IN="+invertLow+",NM="+normalMid+",IM="+invertMid + ",NH="+normalHigh+",IH="+invertHigh+",NP="+normalPump+",IP="+invertPump;
        

        // pump sensor error
        if(normalPump==invertPump)
        {
          // if error in pump sensor, switch to backup mechanism
          sensorReport = sensorReport + "\n\r";
          sensorReport = "Pump sensor error.Switching to backup mechanism";
          notifyURL(sensorReport, 1);
          
          forcePumpOn = true;
        }
        else
        {
          health = 0;
          
          // if error in any other sensor then halt
          notifyURL(sensorReport, 1);
          
          beeperOn();
          systemLedOn();
        }
      }
    }
  }
}


/**
 * Do necessary tasks based on events and flags raised
 */
void doMiscTasks()
{
  currentTimeStamp = millis();
  
  if(SENSOR_TEST_EVENT)
  {
    if(currentTimeStamp > 0)
    {
      if(currentTimeStamp > lastSensorTest)
      {
        if(currentTimeStamp - lastSensorTest > SENSOR_TEST_THRESHOLD)
        {
          notifyURL("Running sensor test", 0, 1);
          doSensorTest();
        }
      }
      else
      {
        notifyURL("Running sensor test", 0, 1);
        lastSensorTest = currentTimeStamp;
        doSensorTest();
      }      
    } 
  } 
}



void loop() {  
    
    rtctime = rtc.now();
    
    /*
    if(!inited)
    {
      initSensors();  
    }
    else if(sensorCheck)
    {
      testSensors();
    }
    else
    {
      evaluateAlarms();
      if(health == 1)
      {
        Log.notice("Health OK" CR);
        evaluateTankState();
        doMiscTasks();
      }
    }
    */

    dispatchPendingNotification();
    delay(1000);
}



/**
 * Check rtc
 **/
void checkRTCTime(DateTime dt)
{
  if(dt.year()<2015)
  {
    error = 1;
    trackSystemError(error, "RTC Failure");
  }
}





/**
 * Evaluates the expected alarms
 **/
void evaluateAlarms()
{
  if(!getLocalTime(&timeinfo))
  {
    return;
  }
  else
  {
    // between 5 am and 2 pm or between 5 pm and 7 pm pump runs 
    if(((timeinfo.tm_hour == 5 && timeinfo.tm_min >=30) && timeinfo.tm_hour < 14) || (timeinfo.tm_hour > 5 && timeinfo.tm_hour < 14) || ((timeinfo.tm_hour == 16 && timeinfo.tm_min >=30) && timeinfo.tm_hour < 19) || (timeinfo.tm_hour >= 17 && timeinfo.tm_hour < 19))
    {
      // turn off emergency flag
      if(EMERGENCY_PUMP_EVENT){
        EMERGENCY_PUMP_EVENT = false;
      }
      
      if(!PUMP_EVENT){
        PUMP_EVENT = true;
        notifyURL("Pump alarm time on", 1);
      }
    }
    else if(EMERGENCY_PUMP_EVENT)
    {
      if(!PUMP_EVENT){
        PUMP_EVENT = true;
        notifyURL("Pump alarm time on (emergency)", 1);
      }
    }
    else
    {
      // if emergency flag not on
      if(!EMERGENCY_PUMP_EVENT){
        // no alarms
        if(PUMP_EVENT){
          PUMP_EVENT = false;
          notifyURL("Pump alarm time off", 1);
        }
      }
    }
  
  
    // Sensor test at 3 pm and 3 am
    if((timeinfo.tm_hour == 15 && timeinfo.tm_min == 0 && timeinfo.tm_sec == 0) || (timeinfo.tm_hour == 3 && timeinfo.tm_min == 0 && timeinfo.tm_sec == 0))
    {
      if(!SENSOR_TEST_EVENT){
        SENSOR_TEST_EVENT = true;
      }
    }
    else
    {
      if(SENSOR_TEST_EVENT){
        SENSOR_TEST_EVENT = false;
      }
    }
  }    
}




boolean hasLowChanged()
{
  currentTimeStamp = millis();
  return ((currentTimeStamp - lastLowChange) > SENSOR_STATE_CHANGE_THRESHOLD && lastLowChange > 0);
}


boolean hasMidChanged()
{
  currentTimeStamp = millis();
  return ((currentTimeStamp - lastMidChange) > SENSOR_STATE_CHANGE_THRESHOLD && lastMidChange > 0);
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



/**
 * Updates state of indicators/output to end user for monitoring
 */
void updateIndicators(int &low, int &mid, int &high, int &pump)
{
  // update low indicator
  if(low == 1)
  {
    lowLedOn();
  }
  else
  {
    lowLedOff();
  }


  // update mid indicator
  if(mid == 1)
  {
    midLedOn();
  }
  else
  {
    midLedOff();
  }


  // update high indicator
  if(high == 1)
  {
   highLedOn();
  }
  else
  {
    highLedOff();
  }


  // update high indicator
  if(pump == 1)
  {
    pumpLedOn();
  }
  else
  {
    if(INSUFFICIENTWATER == 1)
    {
      // show indicator
      blinkPumpLed();
    }
    else
    {
      //stop indicator
      pumpLedOff();
    }
  }


   if(willOverflow())
   {
      if(overFlowAlarmStart == 0){
        overFlowAlarmStart = currentTimeStamp;
      }

      if(currentTimeStamp - overFlowAlarmStart < OVERFLOW_ALARM_TIME_THRESHOLD){
        // start alarm
        blinkAlarm();
      }
      else
      {
        // stop alarm
        alarmOff();
      }
   }
   else
   {
      overFlowAlarmStart = 0;
      
      // stop alarm
      alarmOff();
   }


   // update system sensor
   if(SYSTEM_ERROR)
   {
      //blinkSystemLed();
      beeperOn();
   }
   else
   {
      //systemLedOff();
      beeperOff();
   }
}



/**
 * Track system error
 */
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



/**
 * * Track system error with message
 */
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


/**
 * Track insufficient water state
 */
void trackInsufficientWater(int &low, int &mid, int &high, int &pump)
{
  if(low == 0 && mid == 0 && high == 0 && pump == 0)
  {
    INSUFFICIENTWATER = 1;
  }
  else
  {
    INSUFFICIENTWATER = 0;
  }
}



/**
 * Track sensor changes
 */
void trackSensorChanges(int &low, int &mid, int &high, int &pump)
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


  if(mid != tankState.mid)
  {
    if(lastMidChange == 0)
    {
      lastMidChange = currentTimeStamp;
    }
  }
  else
  {
    lastMidChange = 0;
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



/**
 * Track overflow condition
 */
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
  notice.mid = tankState.mid;
  notice.high = tankState.high;
  notice.pump = tankState.pump;
  notice.temperature = temperature;
  message.toCharArray(notice.message, 80);
  notice.queue_time = 0;
  notice.send_time = 0;
  notice.health = health;
  notice.echo = echo;
  notice.error = error;
  notice.debug = debug;
  notice.days_running = daysRunning;

  String timenow = formatRTCTime(rtctime);
  timenow.toCharArray(notice.clocktime, timenow.length()+1);
  
  enqueueNotification(notice);
}



/**
 * Formats RTC DateTime to readable string
 */
String formatRTCTime(DateTime t)
{
  String ft = "";
  ft += String(t.day());
  ft += "/";
  ft += String(t.month());
  ft += "/";
  ft += String(t.year());
  ft += " ";
  ft += String(t.hour());
  ft += ":";
  ft += String(t.minute());
  ft += ":";
  ft += String(t.second());

  return ft;
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
      post+="temperature="+String(notice.temperature);
      post+="&";
      post+="low="+String(notice.low);
      post+="&";
      post+="mid="+String(notice.mid);
      post+="&";
      post+="high="+String(notice.high);
      post+="&";
      post+="pump="+String(notice.pump);
      post+="&";
      post+="queue_time="+String(notice.queue_time);
      post+="&";
      post+="send_time="+String(notice.send_time);
      post+="&";
      post+="error="+String(notice.error);
      post+="&";
      post+="debug="+String(notice.debug);
      post+="&";
      post+="time=" + String(notice.clocktime);
      post+="&";
      post+="days_running=" + String(notice.days_running);     

      return post;
}




/**
 * Send http(s) Notification to remote url with appropriate parameters and custom message
 */
void dispatchPendingNotification()
{
  currentTimeStamp = millis();
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
        http.begin(client, serverName);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        http.addHeader("Host", "iot.flashvisions.com");
        http.addHeader("Content-Length", String(data.length()));
        int httpResponseCode = http.POST(data);       
        Log.trace("HTTP Response code: %d" CR, httpResponseCode);
        http.end();
      }
      else 
      {
        Log.notice("WiFi Disconnected, cannot post data" CR);
      }
      
      posting = false;
      last_notify = currentTimeStamp;
    }
  } 
}
