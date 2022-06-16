//
// Arduino Garden
//

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FS.h>

#include "config.h"


// -----------
// HTTP server

ESP8266WebServer server(80);

static const char TEXT_PLAIN[] PROGMEM = "text/plain";


// -----------
// Keeping track of time

const unsigned long intervalNTP = 86400UL; // Update the time every day
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = 0;
  
uint32_t timeUNIX = 0;                      // The most recent timestamp received from the time server
uint32_t last_watering = 0;
uint32_t stop_watering = 0;
uint32_t last_data_gathering = 0;

uint32_t get_unixtimestamp() {
  return timeUNIX + (millis() - lastNTPResponse) / 1000;
}

String get_formated_time() {

  if (timeUNIX == 0)
    return "";
  
  // Get a time structure
  time_t epochTime = get_unixtimestamp();
  struct tm *ptm = gmtime((time_t *)&epochTime);
  
  char formated_time[16];
  sprintf(formated_time, "%02d/%02d/%04d %02d:%02d", ptm->tm_mday, ptm->tm_mon+1, ptm->tm_year+1900, ptm->tm_hour, ptm->tm_min);

  return (String)formated_time;
}

void log(String data) {
  Serial.println(get_formated_time() + " - " + data);
  File dataLog = SPIFFS.open("/log.txt", "a");
  dataLog.print(get_formated_time() + " - " + data + "\n");
  dataLog.close();
}

void get_internet_time() {

  const char* ntpServerName = "time.nist.gov";

  const int NTP_PACKET_SIZE = 48;  // NTP time stamp is in the first 48 bytes of the message
  byte NTPBuffer[NTP_PACKET_SIZE];     // A buffer to hold incoming and outgoing packets

  IPAddress timeServerIP;        // The time.nist.gov NTP server's IP address
  WiFiUDP UDP;                   // Create an instance of the WiFiUDP class to send and receive UDP messages
  
  UDP.begin(123); 

  if(!WiFi.hostByName(ntpServerName, timeServerIP)) { // Get the IP address of the NTP server
    log("DNS lookup failed. Rebooting.");
    ESP.reset();
  }
  
  log((String)"Time server IP : " + timeServerIP.toString());

  memset(NTPBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  NTPBuffer[0] = 0b11100011;   // LI, Version, Mode
  // send a packet requesting a timestamp:
  UDP.beginPacket(timeServerIP, 123); // NTP requests are to port 123
  UDP.write(NTPBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();  
  int i = 0;
  while (UDP.parsePacket() == 0) {
    if (i > 20)
      ESP.reset();
    delay(500);
    i++;
  }

  UDP.read(NTPBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t NTPTime = (NTPBuffer[40] << 24) | (NTPBuffer[41] << 16) | (NTPBuffer[42] << 8) | NTPBuffer[43];
  
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800UL;
  // subtract seventy years:
  timeUNIX = NTPTime - seventyYears;

  log((String)"NTPTime : " + NTPTime);
  log((String)"Unix epoch time : " + timeUNIX);

  lastNTPResponse = millis();
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
  log(msg);
  server.send(400, FPSTR(TEXT_PLAIN), msg + "\r\n");
}

void replyServerError(String msg) {
  log(msg);
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
  log(message);

  return replyNotFound(message);
}

String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  log("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = getContentType(path);            // Get the MIME type
  if (SPIFFS.exists(path)) {                            // If the file exists
    File file = SPIFFS.open(path, "r");                 // Open it
    size_t sent = server.streamFile(file, contentType); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  log("\tFile Not Found");
  return false;                                         // If the file doesn't exist, return false
}

void setup_http_server() {

  // Default handler for all URIs not defined above
  // Use it to read files from filesystem
  server.onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(server.uri()))                  // send it if it exists
      handleNotFound();                                 // otherwise, respond with a 404 (Not Found) error
  });

  // Start server
  server.begin();
}


// -----------
// 

void startSPIFFS() { // Start the SPIFFS and list all contents
 
  Serial.println(F("Inizializing FS..."));
  if (SPIFFS.begin()){
      Serial.println(F("done."));
  }else{
      Serial.println(F("fail."));
  }

  // To format all space in SPIFFS
  //SPIFFS.format();

  // Get all information of your SPIFFS
  FSInfo fs_info;
  SPIFFS.info(fs_info);

  Serial.println("File sistem info.");

  Serial.print("Total space:      ");
  Serial.print(fs_info.totalBytes);
  Serial.println("byte");

  Serial.print("Total space used: ");
  Serial.print(fs_info.usedBytes);
  Serial.println("byte");

  Serial.print("Block size:       ");
  Serial.print(fs_info.blockSize);
  Serial.println("byte");

  Serial.print("Page size:        ");
  Serial.print(fs_info.totalBytes);
  Serial.println("byte");

  Serial.print("Max open files:   ");
  Serial.println(fs_info.maxOpenFiles);

  Serial.print("Max path length:  ");
  Serial.println(fs_info.maxPathLength);

  Serial.println();

  // Open dir folder
  Dir dir = SPIFFS.openDir("/");
  // Cycle all the content
  while (dir.next()) {
      // get filename
      Serial.print(dir.fileName());
      Serial.print(" - ");
      // If element have a size display It else write 0
      if(dir.fileSize()) {
          File f = dir.openFile("r");
          Serial.println(f.size());
          f.close();
      }else{
          Serial.println("0");
      }
  }
}

void connect_to_wifi() {

  log((String)"Connecting to WIFI " + SSID);

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
  Serial.println("");

  log((String)"IP address: " + WiFi.localIP().toString());
}

void stop_pump() {
  digitalWrite(PUMP, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
}

void start_pump() {
  digitalWrite(PUMP, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
}

void startOTA() {
  ArduinoOTA.setHostname("arduino-garden");
  ArduinoOTA.setPassword("arduino-garden");

  ArduinoOTA.onStart([]() {
    log("OTA is starting");
  });
  ArduinoOTA.onEnd([]() {
    log("OTA is ending");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR) log("OTA Error - Auth Failed");
    else if (error == OTA_BEGIN_ERROR) log("OTA Error - Begin Failed");
    else if (error == OTA_CONNECT_ERROR) log("OTA Error - Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) log("OTA Error - Receive Failed");
    else if (error == OTA_END_ERROR) log("OTA Error - End Failed");
  });
  ArduinoOTA.begin();
  log("OTA ready");
}

void setup() {
  pinMode(PUMP, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(MOISTURE_SENSOR, INPUT);

  digitalWrite(PUMP, LOW);  
  digitalWrite(LED_BUILTIN, HIGH);  

  Serial.begin(115200);
  delay(500);

  Serial.println();

  startSPIFFS();

  connect_to_wifi();
  
  get_internet_time();
  
  setup_http_server();
 
  startOTA();                  // Start the OTA service

  log("------------");
  log("Setup is done\n");
  log("Internet time : " + get_formated_time());
  log((String)"Watering intervals in hours : " + WATERING_INTERVALS_IN_HOURS);
  log((String)"Watering duration in seconds : " + WATERING_DURATION_SEC);
  log((String)"Moisture threashold : " + MOISTURE_THREASHOLD);
}

void gather_data() {
  int moisture_sensor = analogRead(MOISTURE_SENSOR);
  bool pump_status = digitalRead(PUMP);

  log((String)"Moisture sensor : " + moisture_sensor + " - Pump : " + pump_status);

  File dataLog = SPIFFS.open("/data.csv", "a"); // Write the time and the temperature to the csv file
  dataLog.print(get_unixtimestamp());
  dataLog.print(',');
  dataLog.print(moisture_sensor);
  dataLog.print(',');
  dataLog.print(pump_status);
  dataLog.print('\n');
  dataLog.close();
}

void watering_check() {
  if (analogRead(MOISTURE_SENSOR) > MOISTURE_THREASHOLD){
    log("It's watering time");
    start_pump();
    stop_watering = get_unixtimestamp() + WATERING_DURATION_SEC;
  } else {
    log("No need to water the plants");
  }
}

void loop() {  

  // Time to update our internet time
  if (get_unixtimestamp() - prevNTP > intervalNTP) { // Request the time from the time server every day
    prevNTP = get_unixtimestamp();
    get_internet_time();
  }

  // Check if we need to switch the pump off
  if (digitalRead(PUMP) && (get_unixtimestamp() >= stop_watering)) {
    stop_pump();
  }

  // Time to check if the plants need watering
  if (get_unixtimestamp() > last_watering + (WATERING_INTERVALS_IN_HOURS * 60 * 60)) {
    last_watering = get_unixtimestamp();
    watering_check();
  }

  // Time to gather data
  if(get_unixtimestamp() > last_data_gathering + HISTORY_STEP_IN_SEC) {
    last_data_gathering = get_unixtimestamp();
    gather_data();
  }

  server.handleClient();
  ArduinoOTA.handle();                        // listen for OTA events
}
