#include <Wire.h>
#include <RTClib.h> // https://github.com/adafruit/RTClib
#include <LiquidCrystal_I2C.h> // https://github.com/fdebrabander/Arduino-LiquidCrystal-I2C-library

#include <WiFiManager.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

#include <QueueArray.h>
#include <ArduinoLog.h>

#define SENSOR_1_LEVEL 16
#define SENSOR_1_DATA 34

// top sensor
#define SENSOR_2_LEVEL 17
#define SENSOR_2_DATA 35

// middle sensor
#define SENSOR_3_LEVEL 18 //level - blue | black
#define SENSOR_3_DATA 36 //data - bluewhite | yellow

// bottom sensor
#define SENSOR_4_LEVEL 19 //level - greenwhite/white | black
#define SENSOR_4_DATA 39 //data - green | yellow

// indicators
#define ALARM 33
#define LED_MID 32
#define LED_HIGH 27
#define LED_SYSTEM 26
#define LED_PUMP 25
#define LED_LOW 4
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

    /* Init LCD */
    
    lcd.init();
    lcd_print("  INITIALIZING ", 0, 0);
    delay(2000);



    /* INIT RTC */
    
    if (!rtc.begin()) 
    {
      Log.notice("Couldn't find RTC" CR);
      lcd_print(" Couldn't find RTC ", 0, 0);   
      
      while (1){
        Log.notice("Please check RTC" CR);
        delay(2000);
      }
    }
    else
    {
      rtc.adjust(DateTime(__DATE__, __TIME__));
    } 
    


    /* INIT WIFI */
    
    
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP 
    wm.setConfigPortalBlocking(false);
    
    chipid=ESP.getEfuseMac();//The chip ID is essentially its MAC address(length: 6 bytes).
    //Serial.printf("ESP32 Chip ID = %04X",(uint16_t)(chipid>>32));//print High 2 bytes
    //Serial.printf("%08X\n",(uint32_t)chipid);//print Low 4bytes.
    

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

    /* INIT PINS */
    pinMode(BEEPER, OUTPUT);
    digitalWrite(BEEPER, LOW);

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

    pinMode(ALARM, OUTPUT);
    digitalWrite(ALARM, LOW);

    /* Misc init */  
    initialReadTime = millis(); 


    lcd_print("   READY   ", 0, 0, false);
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


void loop() {
    wm.process();
}




void notifyURL(String message, int error)
{
  notifyURL(message, error, 0);
}


void notifyURL(String message, int error, int debug)
{
  Log.trace("Preparing notification" CR);
  
  Notification notice = {};
  notice.low = tankState.low;
  notice.mid = tankState.mid;
  notice.high = tankState.high;
  notice.pump = tankState.pump;
  message.toCharArray(notice.message, 80);
  notice.queue_time = 0;
  notice.send_time = 0;
  notice.health = health;
  notice.echo = echo;
  notice.error = error;
  notice.debug = debug;
  notice.days_running = daysRunning;
  //notice.clocktime
  
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
 * 
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
      
      posting = true;
      
      Notification notice;

      Log.trace("Popping notification from queue. Current size = %d" CR, queue.count());

      notice = queue.dequeue();        
      notice.send_time = millis();
      data = getPostNotificationString(notice);

      Log.notice("Connecting to server" CR);
      http.begin(client, server);
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      int httpResponseCode = http.POST(data);     
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      http.end();
      
      posting = false;
      last_notify = currentTimeStamp;
    }
  } 
}
