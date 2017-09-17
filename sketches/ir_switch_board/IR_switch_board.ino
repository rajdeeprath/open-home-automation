#include <IRremote.h>
#include <EEPROM.h>
#include "Timer.h"

#define RELAY_POWER 10
#define RELAY_1 5
#define RELAY_2 6
#define IR 8
#define BEEPER 3

#define ONE "ff30cf"
#define TWO "ff18e7"
#define PROGRAM "ff906f"
#define MINUS_ONE "ffe01f"
#define PLUS_ONE "ffa857"


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
int timer;
long timer_time;
int TIMER_TARGET;
int timerEvent;

long lastKeyPress;
long keyPressDelay;

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

  // POWER UP RELAY
  pinMode(RELAY_POWER, OUTPUT);
  digitalWrite(RELAY_POWER, HIGH);  
  
  pinMode(IR, INPUT);
  pinMode(BEEPER, OUTPUT);

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
          TIMER_TARGET = RELAY_1;
          debugPrint(String("Timer target set to ... " + TIMER_TARGET));
          PROGMODE = false;
          startTimer(); 
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
          TIMER_TARGET = RELAY_2;
          debugPrint(String("Timer target set to ... " + TIMER_TARGET));
          PROGMODE = false;
          startTimer();
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
          beep();
        }
        else
        {
          debugPrint("Exiting program mode...");
          PROGMODE = false;
          
          // discard timer if active
          beep();
          beep();
          clearTimer();
        }
      }
      else if(code == MINUS_ONE)
      {
        if(timer > 0)
        {
          timer = timer - 2; 
          beep();
          debugPrint(String(timer));
        }
      }
      else if(code == PLUS_ONE)
      {
         timer = timer + 2; 
         beep();
         debugPrint(String(timer));
      }
      
      lastKeyPress = millis();
    }

    irrecv.resume(); // Receive the next value
  }
}



void clearTimer()
{
   // clear timer
   t.stop(timerEvent);
 
   timer = 0;
   TIMER_TARGET = 0;

   beep();
   debugPrint("Existing timer stopped...");
}


void startTimer()
{
  // start timer
  t.stop(timerEvent);
  
  timer_time = timer * 60000;
  timerEvent = t.after(timer_time, timerDone);

  beep();
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

  beep();
}

void beep()
{
  digitalWrite(BEEPER, HIGH);
  delay(100);
  digitalWrite(BEEPER, LOW);
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
  for (int i = 0; i < EEPROM.length(); i++)
  EEPROM.write(i, 0);
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
