#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DS3231.h>

#include <ArduinoLog.h>
#include <Time.h>

#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

#define countof(a) (sizeof(a) / sizeof(a[0]))


#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

time_t myTime;
DS3231 clock;
RTCDateTime timenow;

int last_minute = 0;
boolean inited = false;

void setup() 
{
  // start serial port:
  Serial.begin(9600);
  
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  Log.begin(LOG_LEVEL_NOTICE, &Serial);
  
  Log.notice("Preparing to start" CR);
  
  initOledDisplay(); 
  initRTC();
  
  Log.notice("Ready" CR);
}



void loop() {

  timenow = clock.getDateTime();
  DS3231_display();
  delay(1000);
  
}


void initRTC()
{
    Log.notice("Initialize DS3231" CR);
    clock.begin();
    //clock.setDateTime(__DATE__, __TIME__);
    timenow = clock.getDateTime();
}


void initOledDisplay()
{
  Log.notice("OLED Display" CR);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }

  display.setTextColor(WHITE, BLACK);

  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
  delay(2000); // Pause for 2 seconds

  // Clear the buffer
  display.clearDisplay();
  display.display();

  DS3231_display();
  
  inited=false;
  
}


void draw_text(byte x_pos, byte y_pos, char *text, byte text_size, const GFXfont *f = NULL) {
  display.setCursor(x_pos, y_pos);
  display.setTextSize(text_size);
  display.setFont(f);
  display.print(text);
  //display.display();
}


void DS3231_display()
{  
  
  boolean shouldUpdate = (last_minute != timenow.minute)?true:false;

  if(shouldUpdate == true || inited == false)
  {
    Log.notice("Updating display" CR);
    
    String hhmm = "";
    String date_part_1 = "";
    String month = "";

    display.clearDisplay();

    if(timenow.hour<10)
    {
      hhmm = hhmm + "0" + timenow.hour;
    }
    else
    {
      hhmm = hhmm + "" + timenow.hour;
    }
    
    hhmm = hhmm + ":";
    
    if(timenow.minute<10)
    {
      hhmm = hhmm + "0" + timenow.minute;
    }
    else
    {
      hhmm = hhmm + "" + timenow.minute;
    }
    
    char hhmm_char[hhmm.length()+1];
    hhmm.toCharArray(hhmm_char, hhmm.length()+1);
    
    if(timenow.dayOfWeek == 1)
    {
      date_part_1 = date_part_1 + "MON";
    }
    else if(timenow.dayOfWeek == 2)
    {
      date_part_1 = date_part_1 + "TUE";
    }
    else if(timenow.dayOfWeek == 3)
    {
      date_part_1 = date_part_1 + "WED";
    }
    else if(timenow.dayOfWeek == 4)
    {
      date_part_1 = date_part_1 + "THU";
    }
    else if(timenow.dayOfWeek == 5)
    {
      date_part_1 = date_part_1 + "FRI";
    }
    else if(timenow.dayOfWeek == 6)
    {
      date_part_1 = date_part_1 + "SAT";
    }
    else if(timenow.dayOfWeek == 7)
    {
      date_part_1 = date_part_1 + "SUN";
    }
    
    date_part_1 = date_part_1 + ", ";
    
    if(timenow.day<10)
    {
      date_part_1 = date_part_1 + "0" + timenow.day;
    }
    else
    {
      date_part_1 = date_part_1 + "" + timenow.day;
    }
    
    
    if(timenow.month == 1)
    {
      date_part_1 = date_part_1 + " JAN";
    }
    else if(timenow.month == 2)
    {
      date_part_1 = date_part_1 + " FEB";
    }
    else if(timenow.month == 3)
    {
      date_part_1 = date_part_1 + " MAR";
    }
    else if(timenow.month == 4)
    {
      date_part_1 = date_part_1 + " APR";
    }
    else if(timenow.month == 5)
    {
      date_part_1 = date_part_1 + " MAY";
    }
    else if(timenow.month == 6)
    {
      date_part_1 = date_part_1 + " JUN";
    }
    else if(timenow.month == 7)
    {
      date_part_1 = date_part_1 + " JUL";
    }
    else if(timenow.month == 8)
    {
      date_part_1 = date_part_1 + " AUG";
    }
    else if(timenow.month == 8)
    {
      date_part_1 = date_part_1 + " SEP";
    }
    else if(timenow.month == 10)
    {
      date_part_1 = date_part_1 + " OCT";
    }
    else if(timenow.month == 11)
    {
      date_part_1 = date_part_1 + " NOV";
    }
    else if(timenow.month == 12)
    {
      date_part_1 = date_part_1 + " DEC";
    }
    
    
    char date_char[date_part_1.length()+1];
    date_part_1.toCharArray(date_char, date_part_1.length()+1);
    
    draw_text(5, 35, hhmm_char, 2, &FreeSerifBold12pt7b);
    draw_text(10, 62, date_char, 1, &FreeSans9pt7b);
    
    display.display();

    last_minute = timenow.minute;
    inited = true;
  }
  
  
}
