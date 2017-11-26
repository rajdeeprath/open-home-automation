#define BUZZER 13

#define SMOKE_SENSOR_1_SWITCH 5
#define SMOKE_SENSOR_2_SWITCH 6
#define SMOKE_SENSOR_1 7
#define SMOKE_SENSOR_2 8
#define PIR 9
#define CHIMNEY_RELAY 10

int smoke_sensor_1_val;
int smoke_sensor_2_val;
int pir_val;
int max_samples = 3;
int pir_samples[3] = {0,0,0};
int sample_index = 0;

boolean smoke_sensor_1_detected = false;
boolean smoke_sensor_2_detected = false;
boolean smoke_event_detected = false;
boolean RELAY_ON = false;
boolean SYSTEM_LISTENING = false;
boolean isBuzzerOn = false;
boolean motion = false;
boolean human = true;
boolean sensorsListening = false;
boolean buzzerPlaying = true;
long buzserLastUpdate = 0;
long systemLastAwakeTime = 0;
long smokeDetectedTime = 0;
long buzzerFrequency = 2000;
long systemAwakeThreshold = 1800000;
long sensor_ready_time = 2000;
long smokeDetectedTimeThreshold = 20000;
long chimneyIdleRuntimeThreshold = 15000;
long chimneylastRuntime = 0;
boolean debug = true;


void setup() {
  
  Serial.begin(9600);

  pinMode(BUZZER, OUTPUT);
  
  pinMode(CHIMNEY_RELAY, OUTPUT);
  digitalWrite(CHIMNEY_RELAY, HIGH);
  
  pinMode(SMOKE_SENSOR_1_SWITCH, OUTPUT);
  pinMode(SMOKE_SENSOR_1, INPUT);

  pinMode(SMOKE_SENSOR_2_SWITCH, OUTPUT);  
  pinMode(SMOKE_SENSOR_2, INPUT);
  
  pinMode(PIR, INPUT);

  smoke_sensor_1_detected = false;
  smoke_sensor_2_detected = false;
  smoke_event_detected = false;
  RELAY_ON = false;
  SYSTEM_LISTENING = false;
  isBuzzerOn = false;
  motion = false;
  human = false;
  sensorsListening = false;
  buzzerPlaying = false;

  buzserLastUpdate = millis();
  systemLastAwakeTime = millis();
  smokeDetectedTime = millis();

  systemAwaken();
}



void loop() 
{
  
  // Read PIR and check for human
  pir_val = digitalRead(PIR);
  collectSample(pir_val);

  if(hasMotion())
  {
    human = true;
  }
  else
  {
    human = false;
  }



  // wake sleeping system
  if(human)
  {
    SYSTEM_LISTENING = true;
    systemLastAwakeTime = millis();
    //debugPrint("SYSTEM LISTENING => ON");
  }
  else
  {
    // enough time passed and no motion
    if(millis() - systemLastAwakeTime > systemAwakeThreshold)
    {
      // machine is not running => deactive system + if no smoke detected recently
      if((millis() - smokeDetectedTime > smokeDetectedTimeThreshold))
      {
        if(!smoke_event_detected) // no smoke => safe to take a break
        {
            if(!RELAY_ON)
            {
              SYSTEM_LISTENING = false;
              debugPrint("SYSTEM LISTENING => OFF");
            }
            else // if machine running and no smoke detected => idle
            {
              SYSTEM_LISTENING = false;
              debugPrint("SYSTEM LISTENING => OFF");

              // Idle for long time ? => turn off
              if(millis() - chimneylastRuntime > chimneyIdleRuntimeThreshold){
                chimneyOff();
              }
            }
        }

      }
    }
  }



  // If system is active
  if(SYSTEM_LISTENING)
  {
      // turn on sensors if not on
      if(isMq2SensorsOn){
        mq2SensorsOn();
      }
    
      // Check first sensor
      smoke_sensor_1_val = digitalRead(SMOKE_SENSOR_1); 
      //debugPrint("smoke_sensor_1_val =" + String(smoke_sensor_1_val));
      if(smoke_sensor_1_val == 1)
      {
        if(!smoke_sensor_1_detected)
        {
          debugPrint("SMOKE/GAS DETECTED");
          
          smoke_sensor_1_detected = true;  
          smokeDetectedTime = millis();  
        }    
      }
      else
      {
        if(smoke_sensor_1_detected)
        {
          smoke_sensor_1_detected = false; 
        }
      }
    
    
      // Check second sensor
      smoke_sensor_2_val = digitalRead(SMOKE_SENSOR_2);
      //debugPrint("smoke_sensor_2_val =" + String(smoke_sensor_2_val));
      if(smoke_sensor_2_val == 1)
      {
        if(!smoke_sensor_2_detected)
        {
          smoke_sensor_2_detected = true;  
          smokeDetectedTime = millis();  
        }    
      }
      else
      {
        if(smoke_sensor_2_detected)
        {
          smoke_sensor_2_detected = false; 
        }
      }
    
    
      //eval
      if(smoke_sensor_1_detected || smoke_sensor_2_detected)
      {
          if(!smoke_event_detected)
          {
            smoke_event_detected = true;
          }
      }
      else
      {
          if(smoke_event_detected)
          {
            smoke_event_detected = false;
          }
      }
  }
  else // if system is deactive => turn off sensors to save shelf-life of sensor
  {
      debugPrint("Entering sleep mode...");
      mq2SensorsOff();
  }


  

  // Do something on  smoke / gas dsetection
  if(smoke_event_detected)
  {
      chimneyOn();
  }


  // play buzzer
  if(isBuzzerOn)
  {
    playBuzzer();
  }

}




void systemAwaken()
{
  SYSTEM_LISTENING = true;
  systemLastAwakeTime = millis();
}



void playBuzzer()
{
  if(millis() - buzserLastUpdate > buzzerFrequency)
  {
    if(!buzzerPlaying)
    {
      digitalWrite(BUZZER, HIGH);
      buzzerPlaying = true;
      buzserLastUpdate = millis();

      debugPrint("Buzzer On");
    }
    else
    {
      digitalWrite(BUZZER, LOW);
      buzzerPlaying = false;
      buzserLastUpdate = millis();

      debugPrint("Buzzer Off");
    }
  }
}



void chimneyOn()
{    
    if(!RELAY_ON) 
    {
      debugPrint("Turning on chimney");
      RELAY_ON = true;
      digitalWrite(CHIMNEY_RELAY, LOW);
            
      chimneylastRuntime = millis();
    }
}



void chimneyOff()
{  
   if(RELAY_ON) 
   {
     debugPrint("Turning off chimney");
     RELAY_ON = false;
     digitalWrite(CHIMNEY_RELAY, HIGH);     
   }
}


void buzzerOn()
{
  if(!isBuzzerOn)
  {
    isBuzzerOn = true;
    digitalWrite(BUZZER, HIGH);
  }
}


void buzzerOff()
{
  if(isBuzzerOn)
  {
    isBuzzerOn = false;
    digitalWrite(BUZZER, LOW);
  }
}


void collectSample(int reading)
{
  if(sample_index > (max_samples - 1)){
    sample_index = 0;
  }  

  //debugPrint("sample_index = " + String(sample_index));
  pir_samples[sample_index] = reading;
  sample_index++;
}



boolean hasMotion()
{
  motion = true;
  for(int i=0; i< max_samples; i++)
  {
    //debugPrint("pir_samples = " + String(pir_samples[i]));
    
    if(pir_samples[i] == 0){
      motion = false;
      break;
    }
  }

  return motion;
}


void mq2SensorsOff()
{ 
  if(sensorsListening)
  {
     debugPrint("Disabling sensors");
     
     digitalWrite(SMOKE_SENSOR_1_SWITCH, LOW);
     digitalWrite(SMOKE_SENSOR_2_SWITCH, LOW);
     sensorsListening = false;
  }
}



boolean isMq2SensorsOn()
{
  return sensorsListening;
}



void mq2SensorsOn()
{  
  if(sensorsListening)
  {    
     debugPrint("Enabling sensors");
      
     digitalWrite(SMOKE_SENSOR_1_SWITCH, HIGH);
     digitalWrite(SMOKE_SENSOR_2_SWITCH, HIGH);
     sensorsListening = true;
     
     // wait 2 seconds for sensor to be ready before reading
     delay(sensor_ready_time);      
  }
}




void debugPrint(String message) {
  if (debug) {
    Serial.println(message);
  }
}
