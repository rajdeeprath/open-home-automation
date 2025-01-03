#include <RCSwitch.h>
#include <ArduinoLog.h>
#include <timer.h>

#define BELL_RELAY_PIN 6


const unsigned int RFCODE = 73964;
const unsigned long BELL_PRESS_THRESHOLD = 2000;

boolean BELL_RELAY_ON = false;
unsigned long last_bell_on = 0;

auto timer = timer_create_default();
RCSwitch mySwitch = RCSwitch();


void setup() {
  Serial.begin(9600);
  Log.begin(LOG_LEVEL_NOTICE, &Serial);
  
  Log.notice("Initializing...." CR);

  pinMode(BELL_RELAY_PIN, OUTPUT);
  bell_off();
  
  mySwitch.enableReceive(0);  // Receiver on interrupt 0 => that is pin #2
}


void loop() 
{
  timer.tick();
  
  if (mySwitch.available()) 
  {  
    int value = mySwitch.getReceivedValue();
    
    if (value == 0) 
    {
      Log.notice("Unknown encoding" CR);
    } 
    else 
    {
      Serial.print("Received ");
      Serial.print( mySwitch.getReceivedValue() );
      Serial.print(" / ");
      Serial.print( mySwitch.getReceivedBitlength() );
      Serial.print("bit ");
      Serial.print("Protocol: ");
      Serial.println( mySwitch.getReceivedProtocol() );

      unsigned int code = mySwitch.getReceivedValue();
      unsigned int bits = mySwitch.getReceivedBitlength();
      unsigned int protocol = mySwitch.getReceivedProtocol();

      if(code == RFCODE && protocol == 1 && bits == 24 && !is_bell_on())
      {
        bell_on();
        timer.in(BELL_PRESS_THRESHOLD, bell_off);
      }
    }

    mySwitch.resetAvailable();
  }
}


/**
 * Is Bell press time over ?. This is needed to simulate bell press.
 * Might vary from bell to bell. Adjust according to need.
 */
boolean press_bell_wait_time_over()
{
  unsigned int curr = millis();

  if(curr > 0)
  {
      if(curr - last_bell_on > BELL_PRESS_THRESHOLD)
      {
        return true;
      }
  }
  else if(curr == 0)
  {
      last_bell_on = 0;
  }  

  return false;
}



/**
 * Switch PIN to HIGH to TRIGGER RELAY that will send current to bell
 */
void bell_on()
{
  if(BELL_RELAY_ON == false)
  {
      Log.notice("Switching on bell...." CR);
      digitalWrite(BELL_RELAY_PIN, HIGH);
      BELL_RELAY_ON = true;
      last_bell_on = millis();
  }
}



/**
 * Check if bell relay is on
 */
boolean is_bell_on()
{
  return BELL_RELAY_ON;
}



/**
 * Switch PIN to LOW to TRIGGER RELAY that will send current to bell
 */
void bell_off()
{
  if(BELL_RELAY_ON == true)
  {
      Log.notice("Switching off bell...." CR);
      digitalWrite(BELL_RELAY_PIN, LOW);
      BELL_RELAY_ON = false;
  }
}
