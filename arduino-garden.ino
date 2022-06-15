//
// Arduino Garden
//

#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "config.h"

// Timer stuff
//
#include "ESP8266TimerInterrupt.h"
#include "ESP8266_ISR_Timer.h"

#define USING_TIM_DIV1                false           // for shortest and most accurate timer
#define USING_TIM_DIV16               false           // for medium time and medium accurate timer
#define USING_TIM_DIV256              true            // for longest timer but least accurate. Default
#define HW_TIMER_INTERVAL_MS          50L
#define TIMER_INTERVAL_60S            60000L

// Init ESP8266 timer 1
ESP8266Timer ITimer;

// Init ESP8266_ISR_Timer
ESP8266_ISR_Timer ISR_Timer;



uint8_t hours = 2;
uint8_t minutes = 0;
bool is_dst = false;


void connect_to_wifi() {

  Serial.println();
  Serial.print("Connecting to WIFI ");
  Serial.print(SSID);

  // Initialisation
  WiFi.persistent(false);
  WiFi.disconnect(true);

  // Connecting to a WiFi network
  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    static int i = 0;

    delay(500);
    Serial.print(".");

    // If the connection takes too long, reset the board
    if (i > 20)
      ESP.restart();
    else
      i++;
  }

  Serial.println(" connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void get_internet_time() {

  // Define NTP Client to get time
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP, "pool.ntp.org", 0);

  // Get time
  timeClient.begin();
  timeClient.update();

  // Get a time structure
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);
  hours = ptm->tm_hour;
  minutes = ptm->tm_min;

  // Compute DST
  //  DST (GMT+1 durring summer time) starts on the last Sunday of March and ends on the last Sunday of October
  // True conditions :
  //  April to Sept
  //  March and (mday-wday)>=25
  //  October and (mday-wday)<25
  if( ( (ptm->tm_mon > 2) && (ptm->tm_mon < 9) )
   || ( (ptm->tm_mon == 2) && ((ptm->tm_mday - ptm->tm_wday) >= 25) )
   || ( (ptm->tm_mon == 9) && ((ptm->tm_mday - ptm->tm_wday) < 25) ) )
  {
    hours++;
    is_dst = true;
  }
  else
    is_dst = false;

  Serial.println((String)"Internet time : " + hours + (String)":" + minutes);
  Serial.println((String)"DST : " + is_dst);
  Serial.println((String)"tm_mon : " + ptm->tm_mon);
  Serial.println((String)"tm_mday : " + ptm->tm_mday);
  Serial.println((String)"tm_wday : " + ptm->tm_wday);

}

void IRAM_ATTR TimerHandler() {
  ISR_Timer.run();
}

void stop_pump() {
  digitalWrite(PUMP, LOW);
}

void start_pump() {
  digitalWrite(PUMP, HIGH);
}

void one_minute_int() {
  if (minutes >= 59) {
    if (hours >= 23) {
      hours = 0;
    } else
      hours++;
    minutes = 0;
  } else
    minutes++;

  if ((hours == WATERING_HOUR) && (minutes == WATERING_MINUTE)) {        
    if (analogRead(MOISTURE_SENSOR) < MOISTURE_THREASHOLD){
      start_pump(); 
      ISR_Timer.setTimeout(WATERING_DURATION_SEC * 1000, stop_pump);
    }
  }
}

void setup() {
  pinMode(PUMP, OUTPUT);
  digitalWrite(PUMP, LOW);  

  pinMode(MOISTURE_SENSOR, INPUT);

  Serial.begin(115200);
  Serial.println();

  Serial.println("Hello");

  connect_to_wifi();
  get_internet_time();

  // Interval in microsecs
  if (ITimer.attachInterruptInterval(HW_TIMER_INTERVAL_MS * 1000, TimerHandler)) {
    Serial.print(F("Starting ITimer OK, millis() = ")); Serial.println(millis());
  } else {
    Serial.println(F("Can't set ITimer. Select another freq. or timer"));
  }
  
  ISR_Timer.setInterval(TIMER_INTERVAL_60S, one_minute_int);
}


void loop() {
    int moisture_sensor = analogRead(MOISTURE_SENSOR);
    Serial.println(hours + (String)":" + minutes + (String)"Moisture sensor reading : " + moisture_sensor);
    delay(60000);
}
