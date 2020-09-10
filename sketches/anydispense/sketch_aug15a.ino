
#include <Servo.h>
#include <ArduinoLog.h>

#define sensor A0 // Sharp IR GP2Y0A41SK0F (4-30cm, analog)


const int MAX_DISTANCE = 9;
const int CAPTURE_MIN_DISTANCE = 5;
const int CAPTURE_MAX_DISTANCE = 8;
const int DISPENSE_STEP = 2;

/* we check for 2 seconds */
const long MIN_SENSOR_CHECK_DELAY = 100; //ms
const int MAX_SAMPLES = 10;
const int DISPENSE_WAIT_TIME = 500; //ms
const int MAX_DISPENSE_ANGLE = 110;

long currentTimeStamp = 0;
long lastProximityDistanceCheck = 0;
long lastDispenseTime = 0;
int proximity = 0;

struct ProximitySample {
   int distance;
   long timestamp;
};

/*
ProximitySample sample1 {0, 0};
ProximitySample sample2 {0, 0};
ProximitySample sample3 {0, 0};
ProximitySample sample4 {0, 0};
ProximitySample sample5 {0, 0};
ProximitySample sample6 {0, 0};
ProximitySample sample7 {0, 0};
ProximitySample sample8 {0, 0};
ProximitySample sample9 {0, 0};
ProximitySample sample10 {0, 0};
ProximitySample samples[MAX_SAMPLES] = {sample1, sample2, sample3, sample4, sample5, sample6, sample7, sample8, sample9, sample10};
*/

ProximitySample samples[MAX_SAMPLES];

int samples_index = 0;
boolean dispense_condition = false;


Servo myservo;  // create servo object to control a servo
int pos = 0;    // variable to store the servo position
int default_servo_pos = 30;

boolean debug = false;


void populateSamples()
{
  for(int i=0;i<MAX_SAMPLES;i++){
    ProximitySample sample = {0,0};
    samples[i] = sample;
  }
}


void setup()
{
  Serial.begin(9600);
  
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  Log.begin(LOG_LEVEL_NOTICE, &Serial);
  Log.notice("Preparing to start" CR);

  populateSamples();

  myservo.attach(9);  // attaches the servo on pin 9 to the servo object
  resetDispenserArm();
}
 

void loop()
{
  currentTimeStamp = millis();

  // grab samples every MIN_SENSOR_CHECK_DELAY ms
  if((currentTimeStamp - lastProximityDistanceCheck > MIN_SENSOR_CHECK_DELAY) && dispense_condition == false)
  {
    collectSample();
    evaluateProximitySamples();
  }

  evaluateandImplementDispensing();
}


void collectSample()
{
  currentTimeStamp = millis();

  if(debug){
    Log.notice("Proximity check" CR);
  }
  
  proximity = getProximityDistance();    
  lastProximityDistanceCheck = currentTimeStamp;

  if(debug){
    Log.notice("Proximity %d" CR, proximity);
  }

  ProximitySample sample = {0,0};
  sample.distance = proximity;
  sample.timestamp = currentTimeStamp;

  insertNewSample(sample);

}


void clearProximitySamples()
{
  long current = millis();
  for (int i = 0; i < MAX_SAMPLES; i++){ 
        samples[i].distance = 0;
        samples[i].timestamp = current;
    }
}


void insertNewSample(ProximitySample sample)
{  
    // shift elements forward 
    
    int pos = 0;
    int n = MAX_SAMPLES-1;
    for (int i = n; i > pos; i--){ 
        samples[i].distance = samples[i - 1].distance; 
        samples[i].timestamp = samples[i - 1].timestamp; 
    }

    samples[0].distance = sample.distance; 
    samples[0].timestamp = sample.timestamp; 
}


void evaluateProximitySamples()
{    
  int sum = 0, avg = 0;
  long product = 1;
  for (int i = 0; i<MAX_SAMPLES; i++){ 
        sum = sum + samples[i].distance;

        if(debug){
          Log.trace("distance = %d" CR, samples[i].distance);
        }
        
        product = product * samples[i].distance;
  }

  avg = sum/MAX_SAMPLES;

  if(debug){
    Log.trace("average = %d, product = %l" CR, avg, product);
  }
  
  if((avg >= CAPTURE_MIN_DISTANCE && CAPTURE_MIN_DISTANCE <= CAPTURE_MAX_DISTANCE) && product>0){
    if(dispense_condition == false){
      dispense_condition = true;

      if(debug){
       Log.notice("dispense condition true" CR);
      }
    }
  }
}


void evaluateandImplementDispensing()
{
  if(dispense_condition == true and (millis()-lastDispenseTime > DISPENSE_WAIT_TIME)){

    if(debug){
      Log.notice("Dispensing" CR);
    }

    clearProximitySamples();
    
    softDispense();
    resetDispenserArm();
    
    lastDispenseTime = millis();
    dispense_condition = false;

    if(debug){
      Log.notice("dispense condition false" CR);
    }
  }
}


void resetDispenserArm()
{
  myservo.write(default_servo_pos);
  delay(15);
}


void softDispense()
{   
  for (int pos = default_servo_pos; pos <= MAX_DISPENSE_ANGLE; pos += DISPENSE_STEP) { // goes from 0 degrees to 180 degrees
    myservo.write(pos);              // tell servo to go to position in variable 'pos'
    delay(15);                       // waits 15ms for the servo to reach the position

    // cancel and return back to position
    if(getProximityDistance() == 0){
      return; 
    }
  }
}


int getProximityDistance()
{
  float volts = analogRead(sensor)*0.0048828125;  // value from sensor * (5/1024)
  int distance = 13*pow(volts, -1); // worked out from datasheet graph
  
  if (distance <= MAX_DISTANCE){
    return distance;
  }

  return 0;
}
