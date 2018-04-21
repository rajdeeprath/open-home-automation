#include <SPI.h>
#include <Ethernet.h>
#include <Wire.h>
#include <DS3231.h>
#include <QueueArray.h>



// pump state sensor
#define SENSOR_1_POWER 31
#define SENSOR_1_LEVEL 33
#define SENSOR_1_DATA 35

// top sensor
#define SENSOR_2_POWER 37
#define SENSOR_2_LEVEL 39
#define SENSOR_2_DATA 41

// middle sensor
#define SENSOR_3_POWER 43
#define SENSOR_3_LEVEL 45
#define SENSOR_3_DATA 47

// bottom sensor
#define SENSOR_4_POWER 49
#define SENSOR_4_LEVEL 51
#define SENSOR_4_DATA 53

// cooling fan
#define COOLING_FAN 22

// indicators
#define LED_LOW 24
#define LED_MED 26
#define LED_HIGH 28
#define LED_PUMP 30
#define LED_MAINTAINENCE 32

#define BEEPER 12

#define NOTICE_LIMIT 5

// secondary temperature monitor
//#define TEMPERATURE A0




const String NAME="AMU-PC-001";

DS3231 clock;
RTCDateTime dt;
float temperature;

boolean PUMP_EVENT = false;
boolean POWER_SAVER = false;

long last_notify = 0;
long lastBeepStateChange;
boolean systemFault;
boolean beeping;

boolean debug = true;

const long CONSECUTIVE_NOTIFICATION_DELAY = 5000;

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


struct Notification {
   int low;
   int mid;
   int high;
   int pump;
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

boolean posting;





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


  /* Pins init */

  // init pins
  pinMode(BEEPER, OUTPUT);


  // pump sensor
  pinMode(SENSOR_1_POWER, OUTPUT);
  pinMode(SENSOR_1_LEVEL, OUTPUT);
  pinMode(SENSOR_1_DATA, INPUT);
  
  // top sensor
  pinMode(SENSOR_2_POWER, OUTPUT);
  pinMode(SENSOR_2_LEVEL, OUTPUT);
  pinMode(SENSOR_2_DATA, INPUT);
  
  // middle sensor
  pinMode(SENSOR_3_POWER, OUTPUT);
  pinMode(SENSOR_3_LEVEL, OUTPUT);
  pinMode(SENSOR_3_DATA, INPUT);
  
  // bottom sensor
  pinMode(SENSOR_4_POWER, OUTPUT);
  pinMode(SENSOR_4_LEVEL, OUTPUT);
  pinMode(SENSOR_4_DATA, INPUT);

  // give the ethernet module time to boot up:
  delay(5000);
  
  // start the Ethernet connection using a fixed IP address and DNS server:
  Ethernet.begin(mac, ip);
  
  // print the Ethernet board/shield's IP address:
  Serial.print("My IP address: ");
  Serial.println(Ethernet.localIP());

  // wait for ethernet link (1 mins)
  //delay(10000);
}


void loop()
{
  
  dt = clock.getDateTime();
  
  temperature = clock.readTemperature();

  evaluatePumpAlarm();

  dispatchPendingNotification();

  delay(500);
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
void evaluateSensorPowerState()
{
  if(POWER_SAVER)
  {
    
  }
  else
  {
    
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
      data+="low="+String(tankState.low);
      data+="mid="+String(tankState.mid);
      data+="high="+String(tankState.high);
      data+="pump="+String(tankState.pump);
      data+="temperature="+String(temperature);
      data+="time=" + String(clock.dateFormat("d F Y H:i:s",  dt));

      debugPrint(data);
      
      if (client.connect("iot.flashvisions.com",80)) {
      debugPrint("connected");
      client.println("POST /index.php HTTP/1.1");
      client.println("Host: iot.flashvisions.com");
      client.println("Content-Type: application/x-www-form-urlencoded");
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

