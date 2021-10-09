#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoLog.h>


#define TOP_LEVEL 4 //D2
#define TOP_DATA 14 //D5

#define BOTTOM_LEVEL 5 //D1
#define BOTTOM_DATA 12 //D6

#define RELAY_SWITCH 13 //D7


WiFiManager wm;


boolean inited = false;
long initialReadTime = 0;
long minInitialSensorReadTime = 15000;
long minSensorTestReadtime = 15000;
long minHardwareInitializeTime = 20000;


boolean sensorCheck = false;
boolean indicatorCheck = false;
long sensorTestTime = 0;
long indicatorTestTime = 0;
boolean sensorsInvert = false;

int low, high;
long lastLowChange, lastHighChange, lastOverflowCondition;
int normalLow, normalHigh;
int invertLow, invertHigh;


int health = 1;
boolean SENSOR_TEST_EVENT = false;
boolean RESET_EVENT = false;
int INSUFFICIENTWATER = 0;
int SYSTEM_ERROR = 0;
long lastSensorTest = 0;
long lastIndicatorTest = 0;
boolean isPumpSensorNpN = true;


const int EEPROM_LIMIT = 512;
int eeAddress = 0;


struct TankState {
   int low = 0;
   int high = 0;
   long lastupdate = 0;
};


struct Settings {
  int notify = 1;
  long timestamp;
  int endpoint_length;
  int reset = 0;
  char endpoint[50] = "";
};


Settings conf = {};
TankState tankState = {};


void configModeCallback (WiFiManager *myWiFiManager) 
{
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
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
    relay_switch_off();


    /* INIT WIFI */

    wm.setConfigPortalBlocking(false);
    wm.setAPCallback(configModeCallback);
    wm.setConfigPortalTimeout(180);    

    Log.notice("Attempting to connect to network using saved credentials" CR);  
    Log.notice("Connecting to WiFi..");
    
    
    if(wm.autoConnect("AQUA-P-001", "iot@123"))
    {
      Log.notice("Connected to WiFi");
      delay(2000);
    }
    else 
    {
        Log.notice("Configportal running");
    }
}



void relay_switch_on()
{
  Log.notice("Switching on relay" CR);
  digitalWrite(RELAY_SWITCH, LOW);
}


void relay_switch_off()
{
  Log.notice("Switching off relay" CR);
  digitalWrite(RELAY_SWITCH, HIGH);
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




/* First read of sensors as soon as system starts */
void initSensors()
{
  
  // read bottom sensor
  tankState.low = readSensor(BOTTOM_DATA);

  // read top sensot
  tankState.high = readSensor(TOP_DATA);
  // initial read time
  
  if(millis() - initialReadTime > minInitialSensorReadTime)
  {
      inited = true;
      Log.notice("Inited : " CR);

      doSensorTest();
  }
}


void loop() {  

    wm.process();    
    
    if(!inited)
    {
      initSensors();  
      initSettings();      
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
      }
    }


    delay(1000);
}




void initSettings()
{
  readSettings();
  
  if (conf.timestamp<1) 
  {
    Log.trace("Setting defaults" CR);
    
    String url = "0.0.0.0";
    char tmp[url.length() + 1];
    url.toCharArray(tmp, url.length() + 1);

    conf.endpoint_length = url.length();
    memset(conf.endpoint, 0, sizeof(conf.endpoint));
    strncpy(conf.endpoint, tmp, strlen(tmp));
    
    conf.notify = 0;
  }

  // save settings
  writeSettings();
}



void writeSettings()
{
  eeAddress = 0;
  conf.timestamp = millis();

  EEPROM.write(eeAddress, conf.notify);
  eeAddress++;
  EEPROM.write(eeAddress, conf.timestamp);
  eeAddress++;
  EEPROM.write(eeAddress, conf.endpoint_length);
  eeAddress++;
  EEPROM.write(eeAddress, conf.reset);  

  eeAddress++;
  writeEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  EEPROM.commit();

  Log.notice("Conf saved" CR);
}



void writeEEPROM(int startAdr, int len, char* writeString) {
  //yield();
  for (int i = 0; i < len; i++) {
    EEPROM.write(startAdr + i, writeString[i]);
  }
}




void readSettings()
{
  eeAddress = 0;

  conf.notify = EEPROM.read(eeAddress);
  eeAddress++;
  conf.timestamp = EEPROM.read(eeAddress);
  eeAddress++;
  conf.endpoint_length = EEPROM.read(eeAddress);
  eeAddress++;
  conf.reset = EEPROM.read(eeAddress);

  eeAddress++;
  readEEPROM(eeAddress, conf.endpoint_length, conf.endpoint);

  Log.notice("Conf read" CR);
}



void readEEPROM(int startAdr, int maxLength, char* dest) {

  for (int i = 0; i < maxLength; i++) {
    dest[i] = char(EEPROM.read(startAdr + i));
  }
}


void eraseSettings()
{
  Log.notice("Erasing eeprom" CR);  
  for (int i = 0; i < EEPROM_LIMIT; i++)
    EEPROM.write(i, 0);
}
