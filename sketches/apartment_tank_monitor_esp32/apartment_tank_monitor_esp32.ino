#include <Wire.h>
#include <RTClib.h> // https://github.com/adafruit/RTClib
#include <LiquidCrystal_I2C.h> // https://github.com/fdebrabander/Arduino-LiquidCrystal-I2C-library

#include <WiFiManager.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

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
#define ALARM 32
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
   float temperature;
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

char server[] = "iot.flashvisions.com";

QueueArray <Notification> queue;
Settings conf = {};
TankState tankState = {};
SensorState sensors = {};
IndicatorState indicators = {};

boolean posting;
boolean stateChanged = false;


float temperature;
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

WiFiManager wm;
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


void printLocalTime()
{  
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}


void resetWiFiconfig()
{
  wm.resetSettings();
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
      rtc.adjust(DateTime(__DATE__, __TIME__));
    } 
    

    /* INIT WIFI */
    
    
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP 
    wm.setConfigPortalBlocking(false);
    
    chipid=ESP.getEfuseMac();//The chip ID is essentially its MAC address(length: 6 bytes).
    char chipid_str[20];
    sprintf(chipid_str, "ID: %04X%08X", (uint16_t)(chipid>>32), (uint32_t)chipid);
    lcd_print(chipid_str, 0, 0, false);    
    delay(2000);
    

    Log.notice("Attempting to connect to network using saved credentials" CR);    
    if(wm.autoConnect("AutoConnectAP"))
    {
        Log.notice("connected...yeey :)" CR);    
        lcd_print(" WIFI CONNECTED ", 0, 0);
        delay(2000);
        setTimeByNTP();
        printLocalTime();
    }
    else 
    {
        Log.notice("Config portal running" CR);        
    }
    

    /* Misc init */  
    initialReadTime = millis();

    lcd_print(" I AM  READY   ", 0, 0, false);
    
    lcd_print_sensors(false);
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
  //blinkAlarm();
}


void allIndicatorsOff()
{
  lowLedOff();
  midLedOff();
  highLedOff();
  pumpLedOff();
  systemLedOff();
  //alarmOff();
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
      
      //notifyURL("System Reset!\n[" + message + "]", 0, 1);

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



void doSensorTest()
{
  systemLedOn();  
  sensorCheck = true;
  uprightSensorLevels();
  
}



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
          //notifyURL(sensorReport, 1);
          
          forcePumpOn = true;
        }
        else
        {
          health = 0;
          
          // if error in any other sensor then halt
          //notifyURL(sensorReport, 1);
          
          beeperOn();
          systemLedOn();
        }
      }
    }
  }
}


void loop() {
    wm.process();    
    
    rtctime = rtc.now();   
    temperature = rtc.getTemperature();
     
    checkRTCTime(rtctime);

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
      if(health == 1)
      {
        Log.notice("Health OK" CR);
      }
    }
}



/**
 * Check rtc
 **/
void checkRTCTime(DateTime dt)
{
  if(dt.year()<2015)
  {
    error = 1;
    //trackSystemError(error, "RTC Failure");
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
