#include <SPI.h>
#include <Wire.h>
#include <DS3231.h>
#include <Time.h>         //http://www.arduino.cc/playground/Code/Time  
#include <TimeLib.h>


int LIGHT_SENSOR_PIN = A0;

int PIR_SENSOR_PIN = 2;

int LED_PIN = 13;

int RELAY_PIN_1 = 7;

int RELAY_PIN_2 = 9;

int RTC_SDA_SENSOR_PIN = A4;

int RTC_SCL_SENSOR_PIN = A5;


int callibrationTime = 35; // seconds

boolean callibrationDone = false;

int lightVal = 0;

int pirVal = 0;
int pirHistoryIndex = 0;
int MAX_RECORDS = 2;

int PIRSTATE = LOW;
int LIGHTSTATE = LOW;

boolean CONDITION = false;

boolean LIGHT_ALARM_ACTIVE = false;
boolean lightAlarmActive = false;

boolean PUMP_ALARM_ACTIVE = false;
boolean pumpAlarmActive = false;

long lastPirHigh = 0;
long timeNow = 0;

tmElements_t tm;
time_t current_t;

tmElements_t lightOnTime;
time_t lightOn_t;

tmElements_t lightOffTime;
time_t lightOff_t;

tmElements_t pumpOnTime;
time_t pumpOn_t;

tmElements_t pumpOffTime;
time_t pumpOff_t;


long CONDITION_TIMEOUT = 600; // seconds
int LIGHT_VAL_THRESHOLD = 700;

boolean systemOk = false;
boolean debug = false;

DS3231 clock;
RTCDateTime dt;


void initClock()
{
  // Initialize DS3231
  clock.begin();

  // Set sketch compiling time
  //clock.setDateTime(__DATE__, __TIME__);

  dt = clock.getDateTime();
  setSyncProvider(timeProvider);

  if (timeStatus() != timeSet) 
  {
     systemPrint("Unable to sync with the RTC");
     systemOk = false;
  }
  else
  {
     systemPrint("RTC has set the system time"); 
     systemOk = true;
  } 
      
}

uint32_t timeProvider()
{
  dt = clock.getDateTime();
  return dt.unixtime + 3600; 
}


void setup() {


  pinMode(PIR_SENSOR_PIN, INPUT);

  pinMode(RELAY_PIN_1, OUTPUT);

  pinMode(RELAY_PIN_2, OUTPUT);

  pinMode(LED_PIN, OUTPUT);

  
  // init light on alarm time - evening 5:30 PM
  lightOnTime.Hour = 17;
  lightOnTime.Minute = 30;
  lightOnTime.Second = 00;


  // init light off alarm time - evening 9 PM
  lightOffTime.Hour = 21;
  lightOffTime.Minute = 00;
  lightOffTime.Second = 00;

 
  // init pump on alarm time - evening 12:00 AM
  pumpOnTime.Hour = 00;
  pumpOnTime.Minute = 00;
  pumpOnTime.Second = 00;


  // init pump off alarm time - morning 05:00 AM
  pumpOffTime.Hour = 05;
  pumpOffTime.Minute = 00;
  pumpOffTime.Second = 00;
  

  Serial.begin(9600);

  initClock();
}




void loop() {

  dt = clock.getDateTime();

  if(!systemOk){
    systemPrint("System error");
    return;
  }

  // calliberate sensors for the first time during startup / program reset
  if (callibrationDone == false)
  {
    digitalWrite(LED_PIN, HIGH);
    int counter = 0;

    while (counter < callibrationTime)
    {
      //systemPrint("callibrating");
      delay(1000);
      counter++;
    }

    callibrationDone = true;
    digitalWrite(LED_PIN, LOW);
  }

  
  // Delay
  delay(1000);
  

  // Read light sensor value
  lightVal = analogRead(LIGHT_SENSOR_PIN);


  // Read pir sensor value
  pirVal = digitalRead(PIR_SENSOR_PIN);


  // Evaluate pir data
  evaluateMotionState(pirVal, PIRSTATE);


  // Evaluate light data
  evaluateLightState(lightVal, LIGHTSTATE);


  // check RTC - If not ok skip time related code execution
  //rtcOk = !RTC.oscStopped();
  //if(!rtcOk) return;
  

  // Calculate alarm times W.R.T today
  timeNow = now();
  breakTime(timeNow, tm);
  
  lightOn_t = getTimePostSyncAlarmYearMonthDate(tm, lightOnTime);
  lightOff_t = getTimePostSyncAlarmYearMonthDate(tm, lightOffTime);
  pumpOn_t = getTimePostSyncAlarmYearMonthDate(tm, pumpOnTime);
  pumpOff_t = getTimePostSyncAlarmYearMonthDate(tm, pumpOffTime);
  

  /********************************************
     Check for PIR sensor + LIGHT sensor COMBO
   *******************************************/

  if (PIRSTATE == HIGH && LIGHTSTATE == LOW && LIGHT_ALARM_ACTIVE == false)
  {
    if (CONDITION == false && pirHistoryIndex >= MAX_RECORDS)
    {
      CONDITION = true;
      digitalWrite(RELAY_PIN_1, HIGH);
    }
  }
  else
  {
    if (CONDITION == true)
    {
      if (timeNow - lastPirHigh > CONDITION_TIMEOUT)
      {
        CONDITION = false;
        digitalWrite(RELAY_PIN_1, LOW);
      }
    }
    else
    {
      // evaluate light alarm
      lightAlarmActive = isAlarmPeriodActive(timeNow, lightOn_t, lightOff_t);  // do when on alarm
      if(lightAlarmActive)
      {
        if (LIGHT_ALARM_ACTIVE == false)
        {
          LIGHT_ALARM_ACTIVE = true;
          digitalWrite(RELAY_PIN_1, HIGH);
        }
      }
      else
      {
        if (LIGHT_ALARM_ACTIVE == true)
        {
          LIGHT_ALARM_ACTIVE = false;
          digitalWrite(RELAY_PIN_1, LOW);
        }
      }


      // evaluate pump alarm
      pumpAlarmActive = isAlarmPeriodActive(timeNow, pumpOn_t, pumpOff_t);  // do when on alarm
      if(debug){
        systemPrint(String(pumpAlarmActive));
      }
      if(pumpAlarmActive)
      {
        if (PUMP_ALARM_ACTIVE == false)
        {
          PUMP_ALARM_ACTIVE = true;
          digitalWrite(RELAY_PIN_2, HIGH);
        }
      }
      else
      {
        if (PUMP_ALARM_ACTIVE == true)
        {
          PUMP_ALARM_ACTIVE = false;
          digitalWrite(RELAY_PIN_2, LOW);
        }      
      }
    }
  }
}





void evaluateMotionState(int pirVal, int &PIRSTATE)
{
  if (pirVal == LOW)
  {
    if (PIRSTATE == HIGH)
    {
      PIRSTATE = LOW;

      // reset history tracker
      pirHistoryIndex = 0;
    }
  }
  else
  {
    // record in history tracker
    if (pirHistoryIndex < MAX_RECORDS)
    {
      // record when pir state was high recently
      lastPirHigh = now();
      pirHistoryIndex++;
    }

    if (PIRSTATE == LOW)
    {
      PIRSTATE = HIGH;
    }
  }
}





void evaluateLightState(int lightVal, int &LIGHTSTATE)
{
  if (lightVal > LIGHT_VAL_THRESHOLD)
  {
    if (LIGHTSTATE == HIGH)
    {
      LIGHTSTATE = LOW;
    }
  }
  else
  {
    if (LIGHTSTATE == LOW)
    {
      LIGHTSTATE = HIGH;
    }
  }
}



time_t getTimePostSyncAlarmYearMonthDate(tmElements_t source, tmElements_t &destination)
{
  destination.Year = source.Year;
  destination.Month = source.Month;
  destination.Day = source.Day;
  destination.Wday = source.Wday;

  return makeTime(destination);
}



/* 
 *  Calculate to see if current time is within the range of requested start and end alarm times
 */
boolean isAlarmPeriodActive(time_t currentTime, time_t startTime, time_t endTime)
{ 

  long int diff_start = currentTime - startTime;
  long int diff_end = currentTime - endTime;

  if(diff_start >= 0 && diff_end <= 0)
  {
    return true;
  }

  return false;
}



void print_time(time_t t)
{
  Serial.print(hour(t));
  Serial.print(" : ");
  Serial.print(minute(t));
  Serial.print(" : ");
  Serial.print(second(t));
  Serial.print(" : ");
  Serial.print(day(t));
  Serial.print(" : ");
  Serial.print(weekday(t));
  Serial.print(" : ");
  Serial.print(month(t));
  Serial.print(" : ");
  Serial.print(year(t));
}


/**
 * Prints message to serial
 */
void systemPrint(String message){
    Serial.println(message);
}

