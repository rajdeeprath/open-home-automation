#include <SPI.h>
#include <Ethernet.h>
#include <Wire.h>
#include <DS3231.h>

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


// temperature monitor
#define TEMPERATURE A0


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


DS3231 clock;
RTCDateTime dt;
float temperature;



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

  delay(1000);
}



void evaluatePumpAlarm()
{
  if(dt.hour >= 5 && dt.hour <= 12)
  {
    // between 5 am and 12 pm -> morning pump runs 
  } 
  else if(dt.hour >= 17 && dt.hour <= 19) 
  {
    // between 5 pm and 7 pm -> evening pump run evenbt 
  }
  else
  {
    // no alarms
  }
}



// this method makes a HTTP connection to the server:
void httpRequest() 
{
  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  client.stop();

  // if there's a successful connection:
  if (client.connect(server, 80)) 
  {
    Serial.println("connecting...");
    
    // send the HTTP GET request:
    client.println("GET /robots.txt HTTP/1.1");
    client.println("Host: www.flashvisions.com");
    client.println("User-Agent: arduino-ethernet");
    client.println("Connection: close");
    client.println();
  } 
  else 
  {
    Serial.println("connection failed");
  }
}

