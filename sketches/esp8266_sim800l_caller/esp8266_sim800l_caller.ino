#include <SoftwareSerial.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

#define rxGSM D5
#define txGSM D6

const String phone = "+919836555754";
long CALL_TIME_THRESHOLD = 20000;
const String capailities = "{\"name\":\"HMU-CALL-001\",\"devices\":{\"SWITCH1\":{\"get\":\"\/switch\/1\",\"set\":\"\/switch\/1\/set\",\"type\":\"switch\",\"states\":[\"on\",\"off\"]},\"SWITCH2\":{\"get\":\"\/switch\/2\",\"set\":\"\/switch\/2\/set\",\"type\":\"switch\",\"states\":[\"on\",\"off\"]},\"SWITCH3\":{\"get\":\"\/switch\/3\",\"set\":\"\/switch\/3\/set\",\"type\":\"switch\",\"states\":[\"on\",\"off\"]}},\"global\":{\"actions\":{\"get\":\"\/switch\/all\",\"reset\":\"\/reset\",\"info\":\"\/\"}}}";
boolean debug=true;
boolean resetFlag=false;
boolean calling=false;
int callState = 0; // 0 | 1 | 2
int eeAddress = 0;
long current_timestamp = 0;

struct Settings {
   long lastCallTime = 0;
   int reset = 0;
   int callState = 0;
   int phone_length = 0;
   char phone[15] = "";
};

Settings conf ={};
SoftwareSerial sim800(rxGSM, txGSM);
std::unique_ptr<ESP8266WebServer> server;



void handleRoot() {
  server->send(200, "application/json", capailities);
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
  if(server->hasArg("phone"))
  {
    String phone = String(server->arg("phone"));

    if(isValidNumber(phone))
    { 
      if(!calling)
      {
        server->send(200, "text/plain", "CALLING=" + phone);
        
        calling = true;
        conf.callState = 1; conf.lastCallTime = millis(); conf.phone_length = phone.length();
        phone.toCharArray(conf.phone, phone.length()+1);
        
      }
      else
      {
        server->send(400, "text/plain", "Call in progress");
      }
    }
    else
    {
      server->send(400, "text/plain", "Invalid phone number");
    }
  }
  else
  {
    server->send(400, "text/plain", "No phone number provided");
  }
}



void cancelCall()
{
  server->send(200, "text/plain", "OK");
  calling = false;
}


void setup() {
  Serial.begin(115200);
  Serial.println("Arduino serial initialize");

  Serial.println("Starting...");
  delay(15000);  
  
  sim800.begin(9600);
  Serial.println("SIM800L serial initialize");

  sim800.listen();
  delay(1000);

  sim800.println("AT"); //Once the handshake test is successful, it will back to OK
  updateSerial();
}


void loop() 
{
  current_timestamp = millis();
  
  if(calling)
  {  
    if(conf.callState == 1) // if call init state then set it to progress and invoke sim800L
    {   
      String atcommand = "ATD+ " + String(conf.phone) + ";";
      sim800.println(atcommand);
      conf.callState = 2; // in progress
    }

    // if call in init or in progress state for more than THRESHOLD then cancel call
    if(conf.callState == 1 || conf.callState == 2) 
    {
      if(current_timestamp - conf.lastCallTime > CALL_TIME_THRESHOLD)
      {
        calling = false; 
      }     
    }
  }
  else
  {
    // if call in init or in progress state for more than THRESHOLD then abort call
    if(conf.callState != 0) 
    {
      conf.callState = 0;
      sim800.println("ATH"); //hang up        
    }
  }
}



void updateSerial()
{
  delay(500);
  while (Serial.available()) 
  {
    sim800.write(Serial.read());//Forward what Serial received to Software Serial Port
  }
  while(sim800.available()) 
  {
    Serial.write(sim800.read());//Forward what Software Serial received to Serial Port
  }
}



boolean isValidNumber(String str)
{  
  for(byte i=0;i<str.length();i++)
  {
    if((i == 0 && (str.charAt(i) != '+')) || (!isDigit(str.charAt(i))))
    {
      return false;
    }
  }
  return true;
}
