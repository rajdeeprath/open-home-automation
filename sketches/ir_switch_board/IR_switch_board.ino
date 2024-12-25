#include <IRremote.h>
#include <EEPROM.h>
#include "Timer.h"

#define RELAY_1 5
#define RELAY_2 6
#define IR 8
#define BEEPER 3
#define PROG_INDICATOR 10
#define TIMER_INDICATOR 11

#define ONE "ff30cf"
#define TWO "ff18e7"
#define PROGRAM "ff906f"
#define MINUS_ONE "ffe01f"
#define PLUS_ONE "ffa857"
#define CANCEL "ff6897"
#define TIME_JUMP 10

 
boolean RELAY_1_ON = false;
boolean RELAY_2_ON = false;

boolean debug=true;
boolean stateless = true;
int eeAddress = 0;

struct Settings {
   int relay_1;
   int relay_2;
   long timestamp;
};

Settings conf = {};

IRrecv irrecv(IR);
decode_results results;

String code;
boolean PROGMODE = false;
boolean PROG_INDICATOR_ON = false;
int timer;
long timer_time;
int TIMER_TARGET;
boolean HAS_TIMER_TARGET = false;
boolean TIMER_INDICATOR_ON = false;
int timerEvent;
long lastKeyPress;
long keyPressDelay;

long beeperStartTime;
long beepTimeDelay; // milliseconds
boolean beepMode=false;
int LONG_BEEP = 500;
int SHORT_BEEP = 100;


long blinkStartTime;
long blinkTimeDelay; // milliseconds
boolean blinkMode=false;
int LONG_BLINK = 500;
int SHORT_BLINK = 100;

Timer t;

void setup() 
{
  Serial.begin(9600); 

  readSettings();
  stateless = (conf.timestamp < 0)? true : false;
    
  conf.relay_1 = 0;
  conf.relay_2 = 0;
  conf.timestamp = millis();
  
  pinMode(RELAY_1, OUTPUT);
  digitalWrite(RELAY_1, HIGH);
  
  pinMode(RELAY_2, OUTPUT);
  digitalWrite(RELAY_2, HIGH);

 
  pinMode(IR, INPUT);
  pinMode(BEEPER, OUTPUT);
  pinMode(PROG_INDICATOR, OUTPUT);
  pinMode(TIMER_INDICATOR, OUTPUT);

  irrecv.enableIRIn(); // Start the receiver

  if(stateless) 
  {
    saveSettings();
  }
  else
  {
    restoreSystem();
  }
}


void loop() {
  t.update();
  readIR();
  updateIndicators();
}


void readIR()
{
  if (irrecv.decode(&results)) 
  {
    code = String(results.value, HEX);
    debugPrint(code);
    keyPressDelay = (millis() - lastKeyPress);
    debugPrint("last keypress " + String(keyPressDelay));
    if(keyPressDelay > 1000)
    { 
      if(code == ONE)
      {
        if(PROGMODE)
        {
          if(timer > 0)
          {
            TIMER_TARGET = RELAY_1;
            debugPrint(String("Timer target set to ... " + TIMER_TARGET));
            PROGMODE = false;
            startTimer(); 

            beep();
          }
        }
        else
        {
          toggleRelay(RELAY_1, RELAY_1_ON);
          conf.relay_1 = RELAY_1_ON;
          saveSettings();

          beep();
        }
      }
      else if(code == TWO)
      {
        if(PROGMODE)
        {
          if(timer > 0)
          {
            TIMER_TARGET = RELAY_2;
            debugPrint(String("Timer target set to ... " + TIMER_TARGET));
            PROGMODE = false;          
            startTimer();

            beep();
          }
        }
        else
        {
          toggleRelay(RELAY_2, RELAY_2_ON);
          conf.relay_2 = RELAY_2_ON;
          saveSettings();

          beep();
        }
      }
      else if(code == PROGRAM)
      {
        if(!PROGMODE)
        {
          debugPrint("Entering program mode...");
          if(TIMER_TARGET != 0){
            clearTimer();
          }
  
          PROGMODE = true;
        }
        else
        {
          debugPrint("Exiting program mode...");
          PROGMODE = false;          
          
          // discard timer if active
          clearTimer();
        }

        beep();
      }
      else if(code == MINUS_ONE)
      {
        if(PROGMODE)
        {
          if(timer > 0)
          {
            timer = timer - TIME_JUMP; 
            debugPrint(String(timer));

            shortBlink();
            beep();
          }
        }
      }
      else if(code == PLUS_ONE)
      {
        if(PROGMODE)
        {
           timer = timer + TIME_JUMP; 
           debugPrint(String(timer));

           shortBlink();
           beep();
        }
      }
      
      lastKeyPress = millis();
    }

    irrecv.resume(); // Receive the next value
  }
}


void updateProgIndicator()
{
  if(PROGMODE)
  {
    if(!PROG_INDICATOR_ON)
    {
      digitalWrite(PROG_INDICATOR, HIGH);
      PROG_INDICATOR_ON = true;
    }
  }
  else
  {
    if(PROG_INDICATOR_ON)
    {
      digitalWrite(PROG_INDICATOR, LOW);
      PROG_INDICATOR_ON = false;
    }
  }
}




void updateIndicators()
{
  updateBlinkerState();
  updateBeeperState();  
  updateTimerIndicator();
  updateProgIndicator();
}




void updateTimerIndicator()
{
  if(HAS_TIMER_TARGET)
  {
    if(!TIMER_INDICATOR_ON)
    {
      digitalWrite(TIMER_INDICATOR, HIGH);
      TIMER_INDICATOR_ON = true;
    }
  }
  else
  {
    if(TIMER_INDICATOR_ON)
    {
      digitalWrite(TIMER_INDICATOR, LOW);
      TIMER_INDICATOR_ON = false;
    }
  }
}




void clearTimer()
{
   // clear timer
   t.stop(timerEvent);
 
   timer = 0;
   TIMER_TARGET = 0;
   HAS_TIMER_TARGET = false;
   debugPrint("Existing timer stopped...");
}


void startTimer()
{
  // start timer
  t.stop(timerEvent);

  if(TIMER_TARGET != 0){
    HAS_TIMER_TARGET = true;
  }
  
  timer_time = timer * 60000;
  timerEvent = t.after(timer_time, timerDone);
  debugPrint("New timer started...");
}


void timerDone()
{
  debugPrint("Timer Complete...");
  
  if(TIMER_TARGET == RELAY_1)
  {
    toggleRelay(RELAY_1, RELAY_1_ON);
  }
  else if(TIMER_TARGET == RELAY_2)
  {
    toggleRelay(RELAY_2, RELAY_2_ON);
  } 

  TIMER_TARGET = 0;
  HAS_TIMER_TARGET = false;
    
  longBeep();
}

void updateBeeperState()
{
  if(beepMode)
  {
    debugPrint("Beepr is on checking when to turn off...");
    
    if(millis() - beeperStartTime > beepTimeDelay){
      beeperOff();
    }
  }
}


void updateBlinkerState()
{
  if(blinkMode)
  {
    debugPrint("Blinker is on checking when to turn off...");
    
    if(millis() - blinkStartTime > blinkTimeDelay){
      blinkerOff();
    }
  }
}


void beep()
{
  beeperOff();
  
  beepTimeDelay = SHORT_BEEP;
  beeperOn();
}


void shortBlink()
{
  blinkerOff();
  
  beepTimeDelay = SHORT_BLINK;
  blinkerOn();
}


void longBeep()
{
  beeperOff();

  beepTimeDelay = LONG_BEEP;
  beeperOn();
}


void beeperOn()
{  
  if(!beepMode)
  {
    debugPrint("Turning beeper on");
    
    beepMode=true;
    beeperStartTime = millis();
    digitalWrite(BEEPER, HIGH);
  }
}


void beeperOff()
{
  if(beepMode)
  {
    debugPrint("Turning beeper off");
    
    beepMode=false;
    digitalWrite(BEEPER, LOW);
  }
}



void blinkerOn()
{  
  if(!blinkMode)
  {
    debugPrint("Turning blinker on");
    
    blinkMode=true;
    blinkStartTime = millis();
    digitalWrite(TIMER_INDICATOR, HIGH);
  }
}


void blinkerOff()
{
  if(blinkMode)
  {
    debugPrint("Turning blinker off");
    
    blinkMode=false;
    digitalWrite(TIMER_INDICATOR, LOW);
  }
}


void saveSettings() 
{
  eeAddress = 0;
  conf.timestamp = millis();
  EEPROM.put(eeAddress, conf);
  debugPrint("Conf saved");


  //debugPrint(String(conf.relay_1));
  //debugPrint(String(conf.relay_2));
  //debugPrint(String(conf.timestamp));
}


void readSettings() 
{
  eeAddress = 0;
  EEPROM.get(eeAddress, conf);

  //debugPrint(String(conf.relay_1));
  //debugPrint(String(conf.relay_2));
  //debugPrint(String(conf.timestamp));
}


void eraseSettings()
{
  for (int i = 0; i < EEPROM.length(); i++){
  EEPROM.write(i, 0);
  }
}

void restoreSystem()
{
  debugPrint("restoreSystem");
  
  readSettings();

  RELAY_1_ON = (conf.relay_1 == 0);
  toggleRelay(RELAY_1, RELAY_1_ON);

  RELAY_2_ON = (conf.relay_2 == 0);
  toggleRelay(RELAY_2, RELAY_2_ON);
}



void toggleRelay(int relay, boolean &stateOn)
{
    if(stateOn == true)
    {
      digitalWrite(relay, LOW);
      stateOn = false;
    }
    else
    {
      digitalWrite(relay, HIGH);
      stateOn = true;
    }
}


void debugPrint(String message){
  if(debug){
    Serial.println(message);
  }
}
