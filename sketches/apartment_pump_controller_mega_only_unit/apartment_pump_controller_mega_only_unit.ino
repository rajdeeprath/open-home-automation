#include <SPI.h>
#include <Ethernet.h>
#include <Wire.h>
#include <DS3231.h>
#include <QueueArray.h>
#include <dht.h>


#define SENSOR_1_POWER 29
#define SENSOR_1_LEVEL 31
#define SENSOR_1_DATA 33

// top sensor
#define SENSOR_2_POWER 28
#define SENSOR_2_LEVEL 30
#define SENSOR_2_DATA 32
//#define SENSOR_2_DATA A15

// middle sensor
#define SENSOR_3_POWER 35 //vcc - orange | brown
#define SENSOR_3_LEVEL 37 //level - blue | black
#define SENSOR_3_DATA 39 //data - bluewhite | yellow

// bottom sensor
#define SENSOR_4_POWER 34 //vcc = brown | brown
#define SENSOR_4_LEVEL 36 //level - greenwhite/white | black
#define SENSOR_4_DATA 38 //data - green | yellow

// indicators
#define ALARM 40
#define LED_MID 42
#define LED_HIGH 43
#define LED_SYSTEM 44
#define LED_PUMP 45
#define LED_LOW 46




#define BEEPER 12

#define NOTICE_LIMIT 5

// secondary temperature monitor
#define TEMPERATURE_SECONDARY A8

const String NAME="AMU-PC-001";

DS3231 clock;
RTCDateTime dt;


boolean PUMP_EVENT = false;
boolean POWER_SAVER = false;
boolean MAINTAINENCE_MODE = false;

long last_notify = 0;
long lastBeepStateChange;
long lastPumpLedUpdate;
long lastSystemLedUpdate;

long currentTimeStamp;

boolean systemFault;
boolean beeping;

boolean debug = false;
int echo = 1;
int error = 0;

String subMessage;

const long CONSECUTIVE_NOTIFICATION_DELAY = 5000;
const long SENSOR_STATE_CHANGE_THRESHOLD = 60000;
const long PUMP_SENSOR_STATE_CHANGE_THRESHOLD = 10000;
const long OVERFLOW_STATE_THRESHOLD = 60000;
const long SENSOR_TEST_THRESHOLD = 120000;


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
   int error=0;
   int debug=0;
};


// assign a MAC address for the ethernet controller.
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
};

// for manual configuration:
IPAddress ip(192, 168, 1, 66);

// fill in your Domain Name Server address here:
IPAddress myDns(10, 10, 52, 65);

// initialize the library instance:
EthernetClient client;

char server[] = "iot.flashvisions.com";

QueueArray <Notification> queue;
Settings conf = {};
TankState tankState = {};
SensorState sensors = {};
IndicatorState indicators = {};

boolean posting;
boolean stateChanged = false;

dht DHT;
float temperature;
boolean useRTCTemperature = false;
boolean inited = false;
long initialReadTime = 0;
long minInitialSensorReadTime = 10000;

boolean sensorCheck = false;
long sensorTestTime = 0;
boolean sensorsInvert = false;

int low, mid, high, pump;
long lastLowChange, lastMidChange, lastHighChange, lastPumpChange, lastOverflowCondition;
int normalLow, normalMid, normalHigh, normalPump;
int invertLow, invertMid, invertHigh, invertPump;

int health = 1;
boolean SENSOR_TEST_EVENT = false;
int INSUFFICIENTWATER = 0;
int SYSTEM_ERROR = 0;
long lastSensorTest = 0;

void setup()
{
  // start serial port:
  Serial.begin(9600);
  
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }


  /* RTC init */

  // Initialize DS3231
  Serial.println("Initialize DS3231");;
  clock.begin();

  // Set sketch compiling time
  clock.setDateTime(__DATE__, __TIME__);


  // give the hardware some time to initialize
  delay(20000);  
  systemPrint("Preparing to start");
  
  
  // start the Ethernet connection using a fixed IP address and DNS server:
  Ethernet.begin(mac, ip);
  
  // print the Ethernet board/shield's IP address:
  Serial.print("My IP address: ");
  Serial.println(Ethernet.localIP());


  /* Pins init */

  // init pins
  pinMode(BEEPER, OUTPUT);


  // pump sensor
  pinMode(SENSOR_1_POWER, OUTPUT); //vcc
  pinMode(SENSOR_1_LEVEL, OUTPUT);//level
  pinMode(SENSOR_1_DATA, INPUT);//data

  pumpSensorOn();


  // top sensor
  pinMode(SENSOR_2_POWER, OUTPUT); //vcc
  pinMode(SENSOR_2_LEVEL, OUTPUT);//level
  pinMode(SENSOR_2_DATA, INPUT);//data

  topSensorOn();


  // middle sensor
  pinMode(SENSOR_3_POWER, OUTPUT); //vcc
  pinMode(SENSOR_3_LEVEL, OUTPUT);//level
  pinMode(SENSOR_3_DATA, INPUT);//data
  
  middleSensorOn();


  // bottom sensor
  pinMode(SENSOR_4_POWER, OUTPUT); //vcc
  pinMode(SENSOR_4_LEVEL, OUTPUT);//level
  pinMode(SENSOR_4_DATA, INPUT);//data

  bottomSensorOn();


  // init indicators
  pinMode(ALARM, OUTPUT);
  digitalWrite(ALARM, LOW);

  pinMode(LED_MID, OUTPUT);
  digitalWrite(LED_MID, LOW);

  pinMode(LED_HIGH, OUTPUT);
  digitalWrite(LED_HIGH, LOW);

  pinMode(LED_SYSTEM, OUTPUT);
  digitalWrite(LED_SYSTEM, LOW);
  
  pinMode(LED_PUMP, OUTPUT);
  digitalWrite(LED_PUMP, LOW);
  
  pinMode(LED_LOW, OUTPUT);
  digitalWrite(LED_LOW, LOW);

  pinMode(BEEPER, OUTPUT);
  digitalWrite(BEEPER, LOW);


  /* Misc init */  
  initialReadTime = millis(); 
}




void allIndicatorsOn()
{
  lowLedOn();
  midLedOn();
  highLedOn();
  pumpLedOn();
  systemLedOn();
  alarmOn();
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



void bottomSensorOn()
{
  // bottom sensor activate if is deactive
  if(sensors.low == 0)
  {
    digitalWrite(SENSOR_4_POWER, HIGH); //vcc
    digitalWrite(SENSOR_4_LEVEL, HIGH); // level

    sensors.low = 1;
  }
}


void bottomSensorOff()
{
  // bottom sensor deactivate if is active
  if(sensors.low == 1)
  {
    digitalWrite(SENSOR_4_POWER, LOW); //vcc
    digitalWrite(SENSOR_4_LEVEL, LOW); // level  

    sensors.low = 0;
  }
}


void middleSensorOn()
{
  // middle sensor activate if is deactive
  if(sensors.mid == 0)
  {
    digitalWrite(SENSOR_3_POWER, HIGH); // vcc
    digitalWrite(SENSOR_3_LEVEL, HIGH); // level

    sensors.mid = 1;
  }
}


void middleSensorOff()
{
  // middle sensor deactivate if is active
  if(sensors.mid == 1)
  {
    digitalWrite(SENSOR_3_POWER, LOW); // vcc
    digitalWrite(SENSOR_3_LEVEL, LOW); // level

    sensors.mid = 0;
  }
}


void topSensorOn()
{
  // top sensor activate if is deactive
  if(sensors.high == 0)
  {
    digitalWrite(SENSOR_2_POWER, HIGH); //vcc
    digitalWrite(SENSOR_2_LEVEL, HIGH); //level

    sensors.high = 1;
  }
}


void topSensorOff()
{
  // top sensor deactivate if is active
  if(sensors.high == 1)
  {
    digitalWrite(SENSOR_2_POWER, LOW); //vcc
    digitalWrite(SENSOR_2_LEVEL, LOW); //level

    sensors.high = 0;
  }
}


void pumpSensorOn()
{
  // pump sensor activate if is deactive
  if(sensors.pump == 0)
  {
    digitalWrite(SENSOR_1_POWER, HIGH);  
    digitalWrite(SENSOR_1_LEVEL, HIGH);

    sensors.pump = 1;
  }
}


void pumpSensorOff()
{
  // pump sensor deactivate if is active
  if(sensors.pump == 1)
  {
    digitalWrite(SENSOR_1_POWER, LOW);  
    digitalWrite(SENSOR_1_LEVEL, LOW);

    sensors.pump = 0;
  }
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

  if(debug){
    systemPrint(String(tankState.pump) + "|" + String(tankState.high) + "|" + String(tankState.mid) + "|" + String(tankState.low));
  }
  
  // initial read time
  
  if(millis() - initialReadTime > minInitialSensorReadTime)
  {
      inited = true;
      systemPrint("inited");
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



void doSensorTest()
{
  systemLedOn();
  
  sensorCheck = true;
  sensorTestTime = millis();
  sensorsInvert = false;

  systemPrint("Starting sensor test");

  digitalWrite(SENSOR_1_LEVEL, HIGH); // level
  digitalWrite(SENSOR_2_LEVEL, HIGH); // level
  digitalWrite(SENSOR_3_LEVEL, HIGH); // level
  digitalWrite(SENSOR_4_LEVEL, HIGH); // level
}




void invertSensorLevels()
{
  sensorTestTime = millis();
  sensorsInvert = true;

  systemPrint("Sensor levels inverted");
      
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

  systemPrint("Stopping sensor test");
  
  digitalWrite(SENSOR_1_LEVEL, HIGH); // level
  digitalWrite(SENSOR_2_LEVEL, HIGH); // level
  digitalWrite(SENSOR_3_LEVEL, HIGH); // level
  digitalWrite(SENSOR_4_LEVEL, HIGH); // level
}



void testSensors()
{
  if(!sensorsInvert)
  {
    systemPrint("Checking normal sensor states");
    
    // read bottom sensor
    normalLow = readSensor(SENSOR_4_DATA);
  
    // read middle sensot
    normalMid = readSensor(SENSOR_3_DATA);
  
    // read top sensot
    normalHigh = readSensor(SENSOR_2_DATA);
  
    // read pump sensor
    normalPump = readSensor(SENSOR_1_DATA);

    if(debug){
      systemPrint(String(normalPump) + "|" + String(normalHigh) + "|" + String(normalMid) + "|" + String(normalLow));
    }

    // change condition after 10seconds
    if(millis() - sensorTestTime > 5000){
      invertSensorLevels();      
    }
  }
  else 
  {
    systemPrint("Checking invert sensor states");
    
    // read bottom sensor
    invertLow = readSensor(SENSOR_4_DATA);
  
    // read middle sensot
    invertMid = readSensor(SENSOR_3_DATA);
  
    // read top sensor
    invertHigh = readSensor(SENSOR_2_DATA);
  
    // read pump sensor
    invertPump = readSensor(SENSOR_1_DATA);

    if(debug){
      systemPrint(String(invertPump) + "|" + String(invertHigh) + "|" + String(invertMid) + "|" + String(invertLow));
    }
    
    // change condition after 10 seconds
    if(millis() - sensorTestTime > 5000)
    {
      // cancel test
      cancelSensorTest();  

      // record last test time
      lastSensorTest = millis();

      // evaluate result
      if(normalLow != invertLow && normalMid != invertMid && normalHigh != invertHigh && normalPump != invertPump)
      {
        health = 1;
        //notifyURL("Sensors functioning normally!");
      }
      else
      {
        beeperOn();
        health = 0;
        systemLedOn();
        notifyURL("Sensors problem detected!", 1);
      }
    }
  }
}



void doMiscTasks()
{
  currentTimeStamp = millis();
  if(SENSOR_TEST_EVENT)
  {
    if((currentTimeStamp > 0) && (currentTimeStamp - lastSensorTest > SENSOR_TEST_THRESHOLD))
    {
      doSensorTest();
    } 
  }  
}



void loop()
{
  dt = clock.getDateTime();

  readEnclosureTemperature();

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
      evaluateTankState();
      doMiscTasks();
    }
  }

  dispatchPendingNotification();
  
  delay(1000);
}




void readEnclosureTemperature()
{
  if(useRTCTemperature)
  {
    temperature = clock.readTemperature();
  }
  else
  {
    DHT.read11(TEMPERATURE_SECONDARY);  
    temperature = DHT.temperature;
  }
  
}




/**
 * Evaluates the expected alarms
 **/
void evaluateAlarms()
{
  if(dt.hour >= 5 && dt.hour <= 12)
  {
    // between 5 am and 12 pm -> morning pump runs 
    PUMP_EVENT = true; 
  } 
  else if(dt.hour >= 17 && dt.hour <= 19) 
  {
    // between 5 pm and 7 pm -> evening pump run evenbt 
    PUMP_EVENT = true;
  }
  else
  {
    // no alarms
    PUMP_EVENT = false;
  }


  // Sensor test at 1 am
  if(dt.hour == 1 && dt.minute == 0)
  {
    SENSOR_TEST_EVENT = true;
  }
  else
  {
    SENSOR_TEST_EVENT = false;
  }
}



int readSensorAnalogToDigital(int pin)
{
  int val = analogRead(pin);
  float volts = val * (5.0 / 1023.0);
 // systemPrint(String(volts));

  return (volts >= 4)?1:0;
}


int readSensor(int pin)
{
  // mock delayed change here
  //long timeSince = millis() - initialReadTime;

  /*
  if(timeSince >= 60000)
  { 
    if(pin == SENSOR_1_DATA){
      return 0;
    }
  }  
  else if(timeSince >= 30000)
  { 
    if(pin == SENSOR_2_DATA){
      return 0;
    }
  }
  */

  
  return digitalRead(pin);
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
    
  
    if(POWER_SAVER)
    {
      if(!PUMP_EVENT)
      {
        // if pump sensor is on turn off pump sensor
        if(sensors.pump == 1)
        {
          pumpSensorOff();
        }
      }
      else
      {
        // if pump sensor is off turn on pump sensor
        if(sensors.pump == 0)
        {
          pumpSensorOn();
        }
      }
  
      // if pump sensor is on turn on all sensors
      if(sensors.pump == 1)
      {
        // switch on low
        if(sensors.low == 0)
        {
          bottomSensorOn();
        }
        
        // switch on mid
        if(sensors.mid == 0)
        {
          middleSensorOn();
        }
  
        // switch on high
        if(sensors.high == 0)
        {
          topSensorOn();
        }
      }
      else
      {
        if(tankState.high == 1)
        {
          high = 1;
          
          // switch off low
          if(sensors.low == 1)
          {
            bottomSensorOff();
          }
    
          // assume water
          low = 1;
    
          // switch on mid
          if(sensors.mid == 0)
          {
            middleSensorOn();
          }
    
          // read mid
          mid = readSensor(SENSOR_3_DATA);
        }
        else if(tankState.mid == 1)
        {
           mid = 1;
          
          // switch off top
          if(sensors.high == 1)
          {
            topSensorOff();
          }
    
          // no water
          high = 0;
    
          // switch on low
          if(sensors.low == 0)
          {
            bottomSensorOn();
          }
    
          // read low
          low = readSensor(SENSOR_4_DATA);
        }
        else if(tankState.low == 1)
        {
          low = 1;
          
          // switch off mid
          if(sensors.mid == 1)
          {
            middleSensorOff();
          }
    
          // no water
          mid = 0;
    
          // switch off top
          if(sensors.high == 1)
          {
            topSensorOff();
          }
    
          // no water
          high = 0;
        }
      }
    }
    else
    {
      // make sure all sensors are on
      
      // switch on low
      if(sensors.low == 0)
      {
        bottomSensorOn();
      }
      
      // switch on mid
      if(sensors.mid == 0)
      {
        middleSensorOn();
      }
  
      // switch on high
      if(sensors.high == 0)
      {
        topSensorOn();
      }
  
      // switch on PUMP
      if(sensors.pump == 0)
      {
        pumpSensorOn();
      }
      
  
      // read sensor data
      low = readSensor(SENSOR_4_DATA);
      mid = readSensor(SENSOR_3_DATA);
      high = readSensor(SENSOR_2_DATA);
      pump = readSensor(SENSOR_1_DATA);
    }

    if(debug){
      systemPrint("=======================================================================");
      systemPrint(String(pump) + "|" + String(high) + "|" + String(mid) + "|" + String(low));
    }
  
    // detect change
    trackSensorChanges(low, mid, high, pump);   
  
  
    // update low level state
    if(hasLowChanged())
    {
      if(low == 1)
      {
        if(pump == 1)
        {
          subMessage = "Water Level risen to 10% ";
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
    if(debug){   
      systemPrint(String(tankState.pump) + "|" + String(tankState.high) + "|" + String(tankState.mid) + "|" + String(tankState.low));
      systemPrint("=======================================================================");
      systemPrint("state changed = " + String(stateChanged));
    }


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
      // start alarm
      alarmOn();
   }
   else
   {
      // stop alarm
      alarmOff();
   }


   // update system sensor
   if(SYSTEM_ERROR)
   {
      blinkSystemLed();
      beeperOn();
   }
   else
   {
      systemLedOff();
      beeperOff();
   }
}



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



void trackOverFlow(int pump, int high)
{
  currentTimeStamp = millis();
  
  // track overflow
  if(pump == 1 && high == 1)
  {
    systemPrint("Overflow condition");    
    
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


void notifyURL(String message, int error)
{
  notifyURL(message, error, 0);
}


void notifyURL(String message, int error, int debug)
{
  systemPrint("Preparing notification");
  
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
  
  
  enqueueNotification(notice);
}


/* Add to Notification queue */
void enqueueNotification(struct Notification notice)
{
   notice.queue_time = millis();

   if(queue.count() < NOTICE_LIMIT){
    //systemPrint("Pushing notification to queue");
    queue.enqueue(notice);
   }
}




/**
 * Prints message to serial
 */
void systemPrint(String message){
    Serial.println(message);
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
      if(debug){
        systemPrint("Running Notification service");
        systemPrint("Popping notification from queue. Current size = " + String( queue.count()));
      }
      
      Notification notice = queue.dequeue();
      notice.send_time = millis();
  
      posting = true;
  
      String data = "";
      data+="amu_pc_001=1";
      data+="&";
      data+="message="+String(notice.message);
      data+="&";
      data+="health="+String(notice.health);
      data+="&";
      data+="echo="+String(notice.echo);
      data+="&";      
      data+="temperature="+String(notice.temperature);
      data+="&";
      data+="low="+String(notice.low);
      data+="&";
      data+="mid="+String(notice.mid);
      data+="&";
      data+="high="+String(notice.high);
      data+="&";
      data+="pump="+String(notice.pump);
      data+="&";
      data+="queue_time="+String(notice.queue_time);
      data+="&";
      data+="send_time="+String(notice.send_time);
      data+="&";
      data+="error="+String(notice.error);
      data+="&";
      data+="debug="+String(notice.debug);
      data+="&";
      data+="time=" + String(clock.dateFormat("d F Y H:i:s",  dt));

      //systemPrint(data);
       
      if (client.connect("iot.flashvisions.com",80)) 
      {
        systemPrint("connected");
        client.println("POST /index.php HTTP/1.1");
        client.println("Host: iot.flashvisions.com");
        client.println("Content-Type: application/x-www-form-urlencoded;");
        client.println("Connection: close");
        client.print("Content-Length: ");
        client.println(data.length());
        client.println();
        client.print(data);
        client.println();
  
        if (client.connected()){
          systemPrint("disconnecting.");
          client.stop();
        }
      }
      
      posting = false;
      last_notify = currentTimeStamp;
    }
  } 
}

