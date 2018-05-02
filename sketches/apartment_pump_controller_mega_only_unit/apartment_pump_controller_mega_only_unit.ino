#include <SPI.h>
#include <Ethernet.h>
#include <Wire.h>
#include <DS3231.h>
#include <QueueArray.h>
#include <dht.h>


// pump state sensor
#define SENSOR_1_POWER 29
#define SENSOR_1_LEVEL 31
#define SENSOR_1_DATA 33
//#define SENSOR_1_DATA A10

// top sensor
#define SENSOR_2_POWER 28
#define SENSOR_2_LEVEL 30
#define SENSOR_2_DATA 32

// middle sensor
#define SENSOR_3_POWER 35 //vcc - orange | brown
#define SENSOR_3_LEVEL 37 //level - blue | black
#define SENSOR_3_DATA 39 //data - bluewhite | yellow

// bottom sensor
#define SENSOR_4_POWER 34 //vcc = brown | brown
#define SENSOR_4_LEVEL 36 //level - greenwhite/white | black
#define SENSOR_4_DATA 38 //data - green | yellow

// indicators
#define LED_MAINTAINENCE 32

#define BEEPER 11

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
boolean systemFault;
boolean beeping;

boolean debug = true;

const long CONSECUTIVE_NOTIFICATION_DELAY = 5000;
const long SENSOR_STATE_CHANGE_THRESHOLD = 60000;

String capabilities = "{\"name\":\"" + NAME + "\",\"devices\":{\"name\":\"Irrigation Pump Controller\",\"actions\":{\"getSwitch\":{\"method\":\"get\",\"path\":\"\/switch\/1\"},\"toggleSwitch\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\"},\"setSwitchOn\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\/on\"},\"setSwitchOff\":{\"method\":\"get\",\"path\":\"\/switch\/1\/set\/off\"}, \"getRuntime\":{\"method\":\"get\",\"path\":\"\/switch\/1\/runtime\"},\"setRuntime\":{\"method\":\"get\",\"path\":\"\/switch\/1\/runtime\",\"params\":[{\"name\":\"time\",\"type\":\"Number\",\"values\":\"60, 80, 100 etc\"}]}}},\"global\":{\"actions\":{\"getNotify\":{\"method\":\"get\",\"path\":\"\/notify\"},\"setNotify\":{\"method\":\"get\",\"path\":\"\/notify\/set\",\"params\":[{\"name\":\"notify\",\"type\":\"Number\",\"values\":\"1 or 0\"}]},\"getNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\"},\"setNotifyUrl\":{\"method\":\"get\",\"path\":\"\/notify\/url\/set\",\"params\":[{\"name\":\"url\",\"type\":\"String\",\"values\":\"http:\/\/google.com\"}]},\"reset\":\"\/reset\",\"info\":\"\/\"}}}";

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


struct Notification {
   int low;
   int mid;
   int high;
   int pump;
   float temperature;
   char message[80] = "";
   long queue_time;
   long send_time;
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

boolean posting;
boolean stateChanged = false;

dht DHT;
float temperature;
boolean useRTCTemperature = false;
boolean inited = false;
long initialReadTime =0;
long minInitialSensorReadTime = 10000;

int low, mid, high, pump;
long lastLowChange, lastMidChange, lastHighChange, lastPumpChange;


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
  pinMode(SENSOR_1_DATA, INPUT_PULLUP);//data

  pumpSensorOn();


  // top sensor
  pinMode(SENSOR_2_POWER, OUTPUT); //vcc
  pinMode(SENSOR_2_LEVEL, OUTPUT);//level
  pinMode(SENSOR_2_DATA, INPUT_PULLUP);//data

  topSensorOn();


  // middle sensor
  pinMode(SENSOR_3_POWER, OUTPUT); //vcc
  pinMode(SENSOR_3_LEVEL, OUTPUT);//level
  pinMode(SENSOR_3_DATA, INPUT_PULLUP);//data
  
  middleSensorOn();


  // bottom sensor
  pinMode(SENSOR_4_POWER, OUTPUT); //vcc
  pinMode(SENSOR_4_LEVEL, OUTPUT);//level
  pinMode(SENSOR_4_DATA, INPUT_PULLUP);//data

  bottomSensorOn();


  /* Misc init */  
  initialReadTime = millis();
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



void readSensors()
{
  // initially we read in all sensors  

  // read bottom sensor
  tankState.low = digitalRead(SENSOR_4_DATA);

  // read middle sensot
  tankState.mid = digitalRead(SENSOR_3_DATA);

  // read top sensot
  tankState.high = digitalRead(SENSOR_2_DATA);

  // read pump sensor
  tankState.pump = digitalRead(SENSOR_1_DATA);

  debugPrint(String(tankState.pump) + "|" + String(tankState.high) + "|" + String(tankState.mid) + "|" + String(tankState.low));

  // initial read time
  if(millis() - initialReadTime > minInitialSensorReadTime){
    if(!inited){
      inited = true;
      debugPrint("inited");
      notifyURL("System Started!");
    }
  }
}



void loop()
{
  dt = clock.getDateTime();

  readSensors();  

  readEnclosureTemperature();

  evaluatePumpAlarm();

  evaluateTankState();

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
 * Evaluates the expected pump on/off state alarm
 **/
void evaluatePumpAlarm()
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
}




/**
 * Evaluates sensor power state. Method considers 'power saver mode' to intelligently turn sensor on/off to extend life and save more power
 **/ 
void evaluateTankState()
{
    if(!inited){
      return;
    }


    stateChanged = false;
    
  
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
          mid = digitalRead(SENSOR_3_DATA);
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
          low = digitalRead(SENSOR_4_DATA);
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
      low = digitalRead(SENSOR_4_DATA);
      mid = digitalRead(SENSOR_3_DATA);
      high = digitalRead(SENSOR_2_DATA);
      pump = digitalRead(SENSOR_1_DATA);
    }
  
  
    // detect change
    trackSensorChanges(low, mid, high, pump);
  
  
    // update low level state
    if(hasLowChanged())
    {
      stateChanged = true;
      tankState.low = low;
    }
  
  
    // update mid level state
    if(hasMidChanged())
    {
      stateChanged = true;
      tankState.mid = mid;
    }
  
  
  
    // update high level state
    if(hasHighChanged())
    {
      stateChanged = true;
      tankState.high = high;
    }
  
  
  
    // update pump level state
    if(hasPumpChanged())
    {
      stateChanged = true;
      tankState.pump = pump;
    }
  
  
    /***************************/
    debugPrint("low = " + String(tankState.low));
    debugPrint("mid = " + String(tankState.mid));
    debugPrint("high = " + String(tankState.high));
    debugPrint("pump = " + String(tankState.pump));
    debugPrint("state changed = " + String(stateChanged));


    // evaluate and dispatch message
    if(stateChanged)
    {
      String message = "";

      // evaluate
      if(tankState.high == 1)
      {
        message = "Water Level @ 100%";
      }
      else if(tankState.mid == 1)
      {
        message = "Water Level @ 50%";
      }
      else if(tankState.low == 1)
      {
        message = "Water Level @ 10%";
      }

      // dispatch
      if(message != ""){
        notifyURL(message);
      }
    }
}



boolean hasLowChanged()
{
  long currentTimeStamp = millis();
  return ((currentTimeStamp - lastLowChange) > SENSOR_STATE_CHANGE_THRESHOLD && lastLowChange > 0);
}


boolean hasMidChanged()
{
  long currentTimeStamp = millis();
  return ((currentTimeStamp - lastMidChange) > SENSOR_STATE_CHANGE_THRESHOLD && lastMidChange > 0);
}



boolean hasHighChanged()
{
  long currentTimeStamp = millis();
  return ((currentTimeStamp - lastHighChange) > SENSOR_STATE_CHANGE_THRESHOLD && lastHighChange > 0);
}


boolean hasPumpChanged()
{
  long currentTimeStamp = millis();
  return ((currentTimeStamp - lastPumpChange) > SENSOR_STATE_CHANGE_THRESHOLD && lastPumpChange > 0);
}


void trackSensorChanges(int &low, int &mid, int &high, int &pump)
{
  long currentTimeStamp = millis();
  
  if(low != tankState.low)
  {
    lastLowChange = currentTimeStamp;
  }
  else
  {
    lastLowChange = 0;
  }


  if(mid != tankState.mid)
  {
    lastMidChange = currentTimeStamp;
  }
  else
  {
    lastMidChange = 0;
  }


  if(high != tankState.high)
  {
    lastHighChange = currentTimeStamp;
  }
  else
  {
    lastHighChange = 0;
  }


  if(pump != tankState.pump)
  {
    lastPumpChange = currentTimeStamp;
  }
  else
  {
    lastPumpChange = 0;
  }
}



/**
 * Resets the state of the device by resetting configuration data and erasing eeprom
 */
void doReset()
{
  conf.lastupdate = 0;   
  conf.endpoint_length = 0;
  conf.reset = 0;
  conf.notify = 1;
  memset(conf.endpoint, 0, sizeof(conf.endpoint));
    
  //eraseSettings();   
  delay(1000);
}




/**
 * Add to Notification queue
 */
void notifyURL(String message)
{
  debugPrint("Preparing notification");
  
  Notification notice = {};
  notice.low = tankState.low;
  notice.mid = tankState.mid;
  notice.high = tankState.high;
  notice.pump = tankState.pump;
  notice.temperature = temperature;
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
    //debugPrint("Pushing notification to queue");
    queue.enqueue(notice);
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
  
      String data = "";
      data+="amu_pc_001=1";
      data+="&";
      data+="message="+String(notice.message);
      data+="&";
      data+="health="+String(1);
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
      data+="time=" + String(clock.dateFormat("d F Y H:i:s",  dt));

      debugPrint(data);
      
      if (client.connect("iot.flashvisions.com",80)) 
      {
        debugPrint("connected");
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
          debugPrint("disconnecting.");
          client.stop();
        }
      }
  
      posting = false;
      last_notify = millis();
    }
  } 
}

