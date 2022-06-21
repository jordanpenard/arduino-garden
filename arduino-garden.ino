//
// Arduino Garden
//

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiConnect.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>

#include "config.h"

ESP8266WebServer server(80);

void log(String data) {
  Serial.println(get_formated_time() + " - " + data);
  File dataLog = SPIFFS.open("/log.txt", "a");
  dataLog.print(get_formated_time() + " - " + data + "\n");
  dataLog.close();
}

// -----------
// Config

struct Config {
  uint32_t watering_intervals_in_hours;
  uint32_t watering_duration_in_seconds;
  uint32_t moisture_threashold;
  uint32_t history_steps_in_seconds;
  char password[64];
};

const char *cfg_filename = "/config.json";
Config config;

// Loads the configuration from a file
void loadConfiguration() {
  // Open file for reading
  File file = SPIFFS.open(cfg_filename, "r");

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<JSON_CONFIG_SIZE> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error)
    log(F("Failed to read file, using default configuration"));

  // Copy values from the JsonDocument to the Config
  config.watering_intervals_in_hours = doc["watering_intervals_in_hours"] | WATERING_INTERVALS_IN_HOURS;
  config.watering_duration_in_seconds = doc["watering_duration_in_seconds"] | WATERING_DURATION_SEC;
  config.moisture_threashold = doc["moisture_threashold"] | MOISTURE_THREASHOLD;
  config.history_steps_in_seconds = doc["history_steps_in_seconds"] | HISTORY_STEP_IN_SEC;
  strlcpy(config.password, 
          doc["password"] | DEFAULT_PASSWORD,
          sizeof(config.password));
  
  // Close the file (Curiously, File's destructor doesn't close the file)
  file.close();
}

// Saves the configuration to a file
void saveConfiguration() {

  // Open file for writing
  File file = SPIFFS.open(cfg_filename, "w");
  if (!file) {
    log(F("Failed to create config file"));
    return;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<JSON_CONFIG_SIZE> doc;

  // Set the values in the document
  doc["watering_intervals_in_hours"] = config.watering_intervals_in_hours;
  doc["watering_duration_in_seconds"] = config.watering_duration_in_seconds;
  doc["moisture_threashold"] = config.moisture_threashold;
  doc["history_steps_in_seconds"] = config.history_steps_in_seconds;
  doc["password"] = config.password;

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    log(F("Failed to write to file"));
  }

  // Close the file
  file.close();
}

void set_config() { 

  if (server.arg("password") != config.password) {
    replyServerError("Wrong password");
    
  } else {

    if (server.arg("watering_intervals_in_hours") != "")
      config.watering_intervals_in_hours = server.arg("watering_intervals_in_hours").toInt();
    if (server.arg("watering_duration_in_seconds") != "")
      config.watering_duration_in_seconds = server.arg("watering_duration_in_seconds").toInt();
    if (server.arg("moisture_threashold") != "")
      config.moisture_threashold = server.arg("moisture_threashold").toInt();
    if (server.arg("history_steps_in_seconds") != "")
      config.history_steps_in_seconds = server.arg("history_steps_in_seconds").toInt();
    if (server.arg("new_password") != "")
      server.arg("new_password").toCharArray(config.password, sizeof(config.password));

    saveConfiguration();
    
    server.send(200, "text/plain", "Config saved");
  }
}


// -----------
// H8120 I2C Humidity and temperature sensor

float humidity;
float temperature_C;

int read_H8120_sensor(){

  Wire.beginTransmission(H8120_I2C_ADDRESS);

  Wire.requestFrom( (int) H8120_I2C_ADDRESS, (int) 4);
  delay(100);
  while (Wire.available() != 4);
  int _humidity_hi = Wire.read();
  int _humidity_lo = Wire.read();
  int _temp_hi = Wire.read();
  int _temp_lo = Wire.read();

  Wire.endTransmission();

  // Get the status (first two bits of _humidity_hi_)
  int _status = (_humidity_hi >> 6);

  if (_status == 0) {
    // Calculate Relative Humidity
    humidity = (float)(((unsigned int) (_humidity_hi & 0x3f) << 8) | _humidity_lo) * 100 / (pow(2,14) - 2);
    // Calculate Temperature
    temperature_C = (float) (((unsigned int) (_temp_hi << 6) + (_temp_lo >> 2)) / (pow(2, 14) - 2) * 165 - 40);
  } else {
    log((String)"H8120 - status : " + _status);
  }

  return _status;
}


// -----------
// WiFi

WiFiConnect wifiConnect;
WiFiClient wifiClient;
HTTPClient httpClient;

void configModeCallback(WiFiConnect *mWiFiConnect) {
  Serial.println("Entering Access Point");
}

void connect_to_wifi() {

  wifiConnect.setDebug(true);
  
  /* Set our callbacks */
  wifiConnect.setAPCallback(configModeCallback);

  //wifiConnect.resetSettings(); //helper to remove the stored wifi connection, comment out after first upload and re upload

    /*
       AP_NONE = Continue executing code
       AP_LOOP = Trap in a continuous loop - Device is useless
       AP_RESET = Restart the chip
       AP_WAIT  = Trap in a continuous loop with captive portal until we have a working WiFi connection
    */
    if (!wifiConnect.autoConnect()) { // try to connect to wifi
      /* We could also use button etc. to trigger the portal on demand within main loop */
      wifiConnect.setAPName("Arduino-Garden");
      wifiConnect.startConfigurationPortal(AP_WAIT);//if not connected show the configuration portal
    }    
}


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
  server.send(200, FPSTR("text/plain"), "");
}

void replyOKWithMsg(String msg) {
  server.send(200, FPSTR("text/plain"), msg);
}

void replyNotFound(String msg) {
  server.send(404, FPSTR("text/plain"), msg);
}

void replyBadRequest(String msg) {
  log(msg);
  server.send(400, FPSTR("text/plain"), msg + "\r\n");
}

void replyServerError(String msg) {
  log(msg);
  server.send(500, FPSTR("text/plain"), msg + "\r\n");
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
  else if (filename.endsWith(".json")) return "application/json";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  log("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = getContentType(path);            // Get the MIME type
  
  if (path.equals(cfg_filename)) {
  
    StaticJsonDocument<JSON_CONFIG_SIZE> doc;

    // Set the values in the document
    doc["watering_intervals_in_hours"] = config.watering_intervals_in_hours;
    doc["watering_duration_in_seconds"] = config.watering_duration_in_seconds;
    doc["moisture_threashold"] = config.moisture_threashold;
    doc["history_steps_in_seconds"] = config.history_steps_in_seconds;

    String output;
    
    if (serializeJson(doc, output) == 0) {
      log(F("Failed to serializeJson to String"));
    }
    
    server.send(200, "application/json", output);
      
    return true;
  }
  
  if (SPIFFS.exists(path)) {                            // If the file exists
    File file = SPIFFS.open(path, "r");                 // Open it
    size_t sent = server.streamFile(file, contentType); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  log("\tFile Not Found");
  return false;                                         // If the file doesn't exist, return false
}

void manual_watering() {

  if (server.arg("password") != config.password) {
    replyServerError("Wrong password");
    
  } else {
    watering_check();
    server.send(200, "text/plain", "Done");
  }
}

void reboot() {

  if (server.arg("password") != config.password) {
    replyServerError("Wrong password");
    
  } else {
    server.send(200, "text/plain", "Done");
    delay(1000);
    ESP.reset();
  }
}

void setup_http_server() {

  server.on("/setConfig", set_config);
  server.on("/manualWatering", manual_watering);
  server.on("/reboot", reboot);
  
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
// Storage

void downloadAndSaveFile(String fileName, String url){

  WiFiClientSecure newSecure;
  newSecure.setInsecure();

  log("[HTTP] begin...\n");
  log(fileName);
  log(url);
  httpClient.begin(newSecure, url);
  
  log("[HTTP] GET...");
  log(url.c_str());
  // start connection and send HTTP header
  int httpCode = httpClient.GET();
  if(httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      log("[HTTP] GET... code:");
      log(httpCode);
      log("[FILE] open file for writing");
      log(fileName.c_str());
      
      File file = SPIFFS.open(fileName, "w");

      // file found at server
      if(httpCode == HTTP_CODE_OK) {

          // get lenght of document (is -1 when Server sends no Content-Length header)
          int len = httpClient.getSize();

          // create buffer for read
          uint8_t buff[128] = { 0 };

          // get tcp stream
          WiFiClient * stream = httpClient.getStreamPtr();

          // read all data from server
          while(httpClient.connected() && (len > 0 || len == -1)) {
              // get available data size
              size_t size = stream->available();
              if(size) {
                  // read up to 128 byte
                  int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                  // write it to Serial
                  //Serial.write(buff, c);
                  file.write(buff, c);
                  if(len > 0) {
                      len -= c;
                  }
              }
              delay(1);
          }

          log("[HTTP] connection closed or file end.");
          log("[FILE] closing file");
          file.close();
          
      }
      
  }
  httpClient.end();
  newSecure.stop();
}

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


// -----------
// 

void stop_pump() {
  digitalWrite(PUMP, HIGH);
  digitalWrite(LED_BUILTIN, HIGH);
}

void start_pump() {
  // The relay is active low with a pull up, so if something gets disconnected the pump is off
  digitalWrite(PUMP, LOW);
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

  digitalWrite(PUMP, HIGH);  
  digitalWrite(LED_BUILTIN, HIGH);  

  Serial.begin(115200);
  delay(500);

  Serial.println();

  Wire.begin();
  
  startSPIFFS();

  connect_to_wifi();
  
  get_internet_time();
  
  setup_http_server();
 
  startOTA();                  // Start the OTA service

  loadConfiguration();
  saveConfiguration();

  // Downloading the latest web pages from github on the branch web-live
  downloadAndSaveFile("/index.html","https://raw.githubusercontent.com/jordanpenard/arduino-garden/web-live/data/index.html");
  downloadAndSaveFile("/graph.js","https://raw.githubusercontent.com/jordanpenard/arduino-garden/web-live/data/graph.js");
  downloadAndSaveFile("/graph.css","https://raw.githubusercontent.com/jordanpenard/arduino-garden/web-live/data/graph.css");

  log("------------");
  log("Setup is done\n");
  log("Internet time : " + get_formated_time());
  log((String)"Watering intervals in hours : " + config.watering_intervals_in_hours);
  log((String)"Watering duration in seconds : " + config.watering_duration_in_seconds);
  log((String)"Moisture threashold : " + config.moisture_threashold);
  log((String)"History steps in seconds : " + config.history_steps_in_seconds);
  Serial.println((String)"Password : " + config.password);
}

void gather_data() {
  int moisture_sensor = analogRead(MOISTURE_SENSOR);
  bool pump_status = !digitalRead(PUMP);
  read_H8120_sensor();

  log((String)"Moisture sensor : " + moisture_sensor + " - Pump : " + pump_status + " - Humidity : " + humidity + " - Temperature : " + temperature_C);

  File dataLog = SPIFFS.open("/data.csv", "a"); // Write the time and the temperature to the csv file
  dataLog.print(get_unixtimestamp());
  dataLog.print(',');
  dataLog.print(moisture_sensor);
  dataLog.print(',');
  dataLog.print(pump_status);
  dataLog.print(',');
  dataLog.print(humidity);
  dataLog.print(',');
  dataLog.print(temperature_C);
  dataLog.print('\n');
  dataLog.close();
}

void watering_check() {
  last_watering = get_unixtimestamp();
  if (analogRead(MOISTURE_SENSOR) > config.moisture_threashold){
    log("It's watering time");
    start_pump();
    stop_watering = get_unixtimestamp() + config.watering_duration_in_seconds;
  } else {
    log("No need to water the plants");
  }
}

void loop() {  

  // Checking if the flash is full
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  if (fs_info.usedBytes >= fs_info.totalBytes) {
    startSPIFFS();
    SPIFFS.format();
    log("Flash storage was full and got wiped clean");
    ESP.reset();
  }

  // Time to update our internet time
  if (get_unixtimestamp() - prevNTP > intervalNTP) { // Request the time from the time server every day
    prevNTP = get_unixtimestamp();
    get_internet_time();
  }

  // Check if we need to switch the pump off
  if (!digitalRead(PUMP) && (get_unixtimestamp() >= stop_watering)) {
    stop_pump();
  }

  // Time to check if the plants need watering
  if (get_unixtimestamp() > last_watering + (config.watering_intervals_in_hours * 60 * 60)) {
    watering_check();
  }

  // Time to gather data
  if(get_unixtimestamp() > last_data_gathering + config.history_steps_in_seconds) {
    last_data_gathering = get_unixtimestamp();
    gather_data();
  }

  server.handleClient();
  ArduinoOTA.handle();                        // listen for OTA events
}
