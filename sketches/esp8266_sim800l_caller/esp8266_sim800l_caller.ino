#include <SoftwareSerial.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ArduinoLog.h>

#define rxGSM D5
#define txGSM D6

const String COUNTRY_CODE = "91";
long CALL_TIME_THRESHOLD = 20000;
const String NAME = "HMU-CALL-001";
const String AP_PASS = "iot@123!";
const String capabilities = "{\"name\":\" " + NAME + "\",\"devices\":{\"SIM\":{\"get\":\"\/call\/get\",\"set\":\"\/call\/set\",\"type\":\"sim\",\"states\":[\"on\",\"off\"]}},\"global\":{\"actions\":{\"reset\":\"\/reset\",\"info\":\"\/\"}}}";
boolean debug=true;
boolean resetFlag=false;
boolean calling=false;
int callState = 0; // 0 | 1 | 2
int eeAddress = 0;
long current_timestamp = 0;
long init_time = 0;

struct Settings {
   int callState = 0;   
   long lastCallTime = 0;
   long lastupdate = 0;
   int reset = 0;   
   int phone_length = 0;
   char phone[15] = "";
};

Settings conf ={};
SoftwareSerial sim800(rxGSM, txGSM);
std::unique_ptr<ESP8266WebServer> server;
WiFiManager wm;


void handleRoot() {
  server->send(200, "application/json", capabilities);
}


void handleReset() 
{
  resetFlag=true;
  server->send(200, "text/plain", "Resetting in 5 seconds");
}


void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server->uri();
  message += "\nMethod: ";
  message += (server->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server->args();
  message += "\n";
  for (uint8_t i = 0; i < server->args(); i++) {
    message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
  }
  server->send(404, "text/plain", message);
}


void doCall()
{ 
  Log.notice("Call requested." CR);
  
  if(server->hasArg("phone"))
  {
    String phone = String(server->arg("phone"));

    if(isValidNumber(phone))
    { 
      Log.notice("Number %s is valid." CR, phone);
      
      if(!calling)
      {
        server->send(200, "text/plain", "CALLING=" + phone);

        Log.notice("Call init." CR);
        
        calling = true;
        conf.callState = 1; conf.lastCallTime = millis(); conf.phone_length = phone.length();
        phone.toCharArray(conf.phone, phone.length()+1);
        
      }
      else
      {
        Log.error("Call already in progress." CR);
        server->send(400, "text/plain", "Call in progress");
      }
    }
    else
    {
      Log.error("Invalid phone number." CR);
      server->send(400, "text/plain", "Invalid phone number");
    }
  }
  else
  {
    Log.error("No phone number provided!" CR);
    server->send(400, "text/plain", "No phone number provided");
  }
}



void cancelCall()
{
  Log.notice("Cancel call requested." CR);
  server->send(200, "text/plain", "CALLING=" + String(conf.callState));
  calling = false;
}


void getCallState()
{
  Log.notice("Returning call state." CR);
  server->send(200, "text/plain", "CALLING=" + String(conf.callState));
}


void setup() {
  Log.notice("Starting.." CR);  
  init_time = millis();
  
  Serial.begin(9600);
  Log.begin(LOG_LEVEL_NOTICE, &Serial);  
  Log.notice("Serial initialize!" CR);    
  
  sim800.begin(9600);
  Log.notice("SIM800L serial initialize!" CR);

  //sim800.listen();
  //delay(1000);

  Serial.println("Initializing...");
  
  sim800.println("AT");
  waitForResponse();

  sim800.println("ATE1");
  waitForResponse();

  sim800.println("AT+CMGF=1");
  waitForResponse();

  sim800.println("AT+CNMI=1,2,0,0,0");
  waitForResponse();

  sim800.println("AT+CPAS");
  waitForResponse();

  sim800.println("AT+CGREG?");
  waitForResponse();

  sim800.println("AT+CSQ");
  waitForResponse();

  char APNAME[NAME.length() + 1];
  NAME.toCharArray(APNAME, NAME.length() + 1);

  char PASS[AP_PASS.length() + 1];
  AP_PASS.toCharArray(PASS, AP_PASS.length() + 1);
  
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(60);
  
  //automatically connect using saved credentials if they exist
  //If connection fails it starts an access point with the specified name
  if(wm.autoConnect(APNAME, PASS)){
      Log.notice("connected...yeey :)");

      server.reset(new ESP8266WebServer(WiFi.localIP(), 80));
      
      server->on("/", handleRoot);
      server->on("/reset", handleReset);
      server->on("/call/get", getCallState);
      server->on("/call/set", doCall);
      server->on("/call/cancel/set", cancelCall);      
      server->onNotFound(handleNotFound);      
      server->begin();
      
      Serial.print("IP = ");      
      Serial.println(WiFi.localIP());
      Log.notice("HTTP server started" CR);
  }
  else 
  {
      Log.warning("Configportal running");
  }  
}

void loop() 
{
  current_timestamp = millis();

  if(resetFlag)
  {
    ESP.restart();
  }
  else
  {
    if(calling)
    {  
      if(conf.callState == 1) // if call init state then set it to progress and invoke sim800L
      {      
        Log.notice("Triggering SIM800L to initiate call!" CR);
        String atcommand = "ATD+" + COUNTRY_CODE + String(conf.phone) + ";";
        sim800.println(atcommand);      
        waitForResponse();
        Log.notice("Setting call state to progress" CR);
        conf.callState = 2; // in progress
      }
      
      // if call in init or in progress state for more than THRESHOLD then cancel call
      if(conf.callState == 1 || conf.callState == 2) 
      {
        //Log.trace("Calling in progress.." CR);      
        if(current_timestamp - conf.lastCallTime > CALL_TIME_THRESHOLD)
        {
          Log.notice("Calling timeout. Ending call" CR);
          calling = false; 
          Log.notice("Call ended." CR);
        }     
      }
    }
    else
    {
      // if call in init or in progress state for more than THRESHOLD then abort call
      if(conf.callState != 0) 
      {
        Log.notice("Hanging up call." CR);
        conf.callState = 0;
        sim800.println("ATH"); //hang up
      }
    }
    
    wm.process();  
    
    if(WiFi.status() == WL_CONNECTED){
      delay(3);
      server->handleClient();
    }  
  }
}



void waitForResponse(){
  delay(1000);
  while(sim800.available()){
    Serial.println(sim800.readString());
  }
  sim800.read();
}




boolean isValidNumber(String str)
{  
  for(byte i=0;i<str.length();i++)
  {
    if(!isDigit(str.charAt(i)))
    {
      return false;
    }
  }
  return true;
}




/**
 * Erases eeprom of all settings
 */
void eraseSettings()
{
  EEPROM.begin(512);
  
  debugPrint("Erasing eeprom...");
  
  for (int i = 0; i < 512; i++){
    EEPROM.write(i, 0);
  }

  EEPROM.commit();
  EEPROM.end();
}




/**
 * Writes active configuration  to eeprom
 */
void writeSettings() 
{
  EEPROM.begin(512);
  
  eeAddress = 0;
  conf.lastupdate = millis();

  EEPROM.write(eeAddress, conf.callState);
  eeAddress++;  
  EEPROM.write(eeAddress, conf.lastCallTime);
  eeAddress++;
  EEPROM.write(eeAddress, conf.lastupdate);
  eeAddress++;
  EEPROM.write(eeAddress, conf.reset);
  eeAddress++;
  EEPROM.write(eeAddress, conf.phone_length);

  eeAddress++;
  writeEEPROM(eeAddress, conf.phone_length, conf.phone);
  EEPROM.commit();

  EEPROM.end();
  
  debugPrint("Conf saved");
  debugPrint(String(conf.callState));
  debugPrint(String(conf.lastCallTime));
  debugPrint(String(conf.lastupdate));
  debugPrint(String(conf.reset));
  debugPrint(String(conf.phone_length));
  debugPrint(String(conf.phone));
}


/**
 * Write string to eeprom
 */
void writeEEPROM(int startAdr, int len, char* writeString) {
  //yield();
  for (int i = 0; i < len; i++) {
    EEPROM.write(startAdr + i, writeString[i]);
  }
}




/**
 * Reads last written configuration object from eeprom
 */
void readSettings() 
{
  EEPROM.begin(512);
  
  eeAddress = 0;
  
  conf.callState = EEPROM.read(eeAddress);
  eeAddress++;
  conf.lastCallTime = EEPROM.read(eeAddress);
  eeAddress++;
  conf.lastupdate = EEPROM.read(eeAddress);
  eeAddress++;
  conf.reset = EEPROM.read(eeAddress);
  eeAddress++;
  conf.phone_length = EEPROM.read(eeAddress);

  eeAddress++;
  readEEPROM(eeAddress, conf.phone_length, conf.phone);

  EEPROM.end();

  debugPrint("Conf read");
  debugPrint(String(conf.callState));
  debugPrint(String(conf.lastCallTime));
  debugPrint(String(conf.lastupdate));
  debugPrint(String(conf.reset));
  debugPrint(String(conf.phone_length));
  debugPrint(String(conf.phone));
}

/**
 * Reads string from eeprom
 */
void readEEPROM(int startAdr, int maxLength, char* dest) {

  for (int i = 0; i < maxLength; i++) {
    dest[i] = char(EEPROM.read(startAdr + i));
  }
}




/**
 * Prints message to serial
 */
void debugPrint(String message){
  Log.trace("%s" CR, message);
}
