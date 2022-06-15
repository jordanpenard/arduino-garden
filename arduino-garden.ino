//
// Arduino Garden
//

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "config.h"


// -----------
// Timer stuff

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


// -----------
// HTTP server

ESP8266WebServer server(80);

static const char TEXT_PLAIN[] PROGMEM = "text/plain";


// -----------
// Data loging

int history_moisture[HISTORY_DEPTH];
bool history_pump[HISTORY_DEPTH];
int history_index = 0;
int history_count = 0;


// -----------
// Keeping track of time

uint8_t hours = 2;
uint8_t minutes = 0;
bool is_dst = false;

String get_time() {
  char formated_time[5];
  sprintf(formated_time, "%02d:%02d", hours, minutes);
  return (String)formated_time;
}


// -----------
// HTTP server

// Utils to return HTTP codes

void replyOK() {
  server.send(200, FPSTR(TEXT_PLAIN), "");
}

void replyOKWithMsg(String msg) {
  server.send(200, FPSTR(TEXT_PLAIN), msg);
}

void replyNotFound(String msg) {
  server.send(404, FPSTR(TEXT_PLAIN), msg);
}

void replyBadRequest(String msg) {
  Serial.println(msg);
  server.send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void replyServerError(String msg) {
  Serial.println(msg);
  server.send(500, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

/*
   The "Not Found" handler catches all URI not explicitly declared in code
   First try to find and return the requested file from the filesystem,
   and if it fails, return a 404 page with debug information
*/
void handleNotFound() {
  String uri = ESP8266WebServer::urlDecode(server.uri()); // required to read paths with blanks

  // Dump debug data
  String message;
  message.reserve(100);
  message = F("Error: File not found\n\nURI: ");
  message += uri;
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  message += server.args();
  message += '\n';
  for (uint8_t i = 0; i < server.args(); i++) {
    message += F(" NAME:");
    message += server.argName(i);
    message += F("\n VALUE:");
    message += server.arg(i);
    message += '\n';
  }
  message += "path=";
  message += server.arg("path");
  message += '\n';
  Serial.print(message);

  return replyNotFound(message);
}

String get_json_data() {
  String json;
  json.reserve(88);
    
  String moisture_data = "[";
  String pump_data = "[";
  String labels = "[";
  bool first = true;
  
  for (int i = 0; i < history_count; i++) {
    int x = i;
    if (history_count == HISTORY_DEPTH) {
      if (history_index + i >= HISTORY_DEPTH)
        x = history_index + i - HISTORY_DEPTH;
      else
        x = history_index + i;
    }

    if (first == false) {
      moisture_data += ", ";
      pump_data += ", ";
      labels += ", ";
    }
    first = false;
    moisture_data += history_moisture[x]/1000.0;
    pump_data += history_pump[x];
    labels += i;
  }
  moisture_data += "]";
  pump_data += "]";
  labels += "]";

  json = "{labels: " + labels + ", datasets: [{";
  json += "  type: 'line',";
  json += "  label: 'Soil moisture',";
  json += "  data: " + moisture_data + ",";
  json += "  fill: false,";
  json += "  borderColor: 'rgb(54, 162, 235)',";
  json += "  backgroundColor: 'rgb(54, 162, 235)',";
  json += "  tension: 0.1";
  json += "},{";
  json += "  type: 'line',";
  json += "  label: 'Pump',";
  json += "  data: " + pump_data + ",";
  json += "  fill: true,";
  json += "  borderColor: 'rgb(255, 99, 132)',";
  json += "  backgroundColor: 'rgba(255, 99, 132, 0.2)',";
  json += "  tension: 0.1";
  json += "}]};";

  return json;
}

String get_webpage() {
  String webpage = "<html><head><script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script></head>\n";
  webpage += "<body><div><canvas id=\"myChart\"></canvas></div><script>\n";
  webpage += "const data = " + get_json_data() + "\n";
  webpage += "const config = {type: 'line', data: data};\n";
  webpage += "const myChart = new Chart(document.getElementById('myChart'), config);\n";
  webpage += "</script></body></html>";
  return webpage;
}

void setup_http_server() {
  
  //get heap status, analog input value and all GPIO statuses in one json call
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", get_webpage());
  });

  // Default handler for all URIs not defined above
  // Use it to read files from filesystem
  server.onNotFound(handleNotFound);

  // Start server
  server.begin();
}


// -----------
// 

void connect_to_wifi() {

  Serial.println();
  Serial.print("Connecting to WIFI ");
  Serial.print(SSID);

  // Initialisation
  WiFi.mode(WIFI_STA);

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

  if (hours == 0 && minutes == 0)
      ESP.restart();

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

  Serial.println((String)"Internet time : " + get_time());
  Serial.println((String)"DST : " + is_dst);
}

void IRAM_ATTR TimerHandler() {
  ISR_Timer.run();
}

void stop_pump() {
  digitalWrite(PUMP, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
}

void start_pump() {
  digitalWrite(PUMP, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
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
    if (analogRead(MOISTURE_SENSOR) > MOISTURE_THREASHOLD){
      start_pump(); 
      ISR_Timer.setTimeout(WATERING_DURATION_SEC * 1000, stop_pump);
    }
  }
}

void setup() {
  pinMode(PUMP, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(MOISTURE_SENSOR, INPUT);

  digitalWrite(PUMP, LOW);  
  digitalWrite(LED_BUILTIN, HIGH);  
  
  Serial.begin(115200);
  Serial.println();

  connect_to_wifi();
  get_internet_time();

  Serial.println((String)"Watering set for " + WATERING_HOUR + (String)":" + WATERING_MINUTE);

  // Interval in microsecs
  if (ITimer.attachInterruptInterval(HW_TIMER_INTERVAL_MS * 1000, TimerHandler)) {
    Serial.print(F("Starting ITimer OK, millis() = ")); Serial.println(millis());
  } else {
    Serial.println(F("Can't set ITimer. Select another freq. or timer"));
  }
  
  ISR_Timer.setInterval(TIMER_INTERVAL_60S, one_minute_int);

  setup_http_server();
}

// Indication that one minute has passed
esp8266::polledTimeout::periodicMs timeToGatherData(HISTORY_STEP_IN_SEC * 1000);

void loop() {  
  server.handleClient();

  if(timeToGatherData) {
    int moisture_sensor = analogRead(MOISTURE_SENSOR);
    
    history_moisture[history_index] = moisture_sensor;
    history_pump[history_index] = digitalRead(PUMP);
  
    Serial.println(get_time() + (String)" - Moisture sensor : " + moisture_sensor + " - Pump : " + digitalRead(PUMP));
  
    if (history_index-1 >= HISTORY_DEPTH)
      history_index = 0;
    else
      history_index++;

    if (history_count < HISTORY_DEPTH)
      history_count++;
  }  
}
