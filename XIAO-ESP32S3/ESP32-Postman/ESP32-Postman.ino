#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <WebSerial.h>
#include <LittleFS.h>
#include <SoftwareSerial.h>
#include <Preferences.h>
#include <Seeed_Arduino_SSCMA.h>
#include <ArduinoJson.h>
#include "time.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "esp_mac.h"

#define SD_CS_PIN 21              // XIAO ESP32-S3 CS pin
#define FIRMWAREVERSION "EO 1.0"  // E-ESP32 O-open source 1.0 Version
#define host "ProjectPostmanESP"

struct deviceInfo {
  String deviceModel;
  uint32_t chipID;
  uint8_t chipCores;
  String MAC_Address;
  String firmware;
  String ssid;
  String wifi_password;
  int32_t signalstrength;
  String hostname;
  IPAddress IP;
  bool wificonnected;
  String uptime;
  long gmtoffset;
  int daylightoffset;
  unsigned long interval;
  bool setup;
};

struct user {
  String username;
  String password;
  String email;
  String phonenumber;
  char pin[6];
  bool admin;
};

const char *AP_ssid = "XIAO_ESP32S3";
const char *AP_password = "123456789";
String ssid = "Thomas";
String password = "Qcpeds*7";
const char *http_username = "admin";
const char *http_password = "admin";

deviceInfo esp32Info = {
  "",         // deviceModel
  0,          // chipID
  0,          // chipCores
  "",         // MAC_Address
  "",         // firmware
  "",         // ssid
  "",         // wifi_password
  0,          // signalstrength
  "",         // hostname
  "",         // IP
  false,      // wificonnected
  "",         // uptime
  -3600 * 5,  // gmtoffset
  0,          // daylightoffset
  30000,      // interval
  false       // setup
};
user userid[6];
AsyncWebServer server(80);       // Create AsyncWebServer object on port 80
SoftwareSerial espSerial(4, 3);  // (D2 = 3) RX, (D3 = 4) TX pins for ESP32
Preferences device;
Preferences userdata;
SSCMA AI;

String lastImage = "";
bool streaming = false;  // Flag to manage streaming state
bool lock = true;        // Flag to manage lock state
unsigned long previousMillis = 0;

String listFiles(bool ishtml = false);
void rebootESP(String message);
void listDirContents(File dir, String path, String &returnText, bool ishtml);
String humanReadableSize(const size_t bytes);
String getContentType(String filename);
bool initSDCard();
void writeFile(fs::FS &fs, const char *path, const char *message);
void appendFile(fs::FS &fs, const char *path, const char *message);
void recordGrove();
void alert(const char *message);
void log(const char *message, bool serial = true);
int year();
int month();
void createDirectories(const char *path);
void setTimezone();
String getInterfaceMacAddress(esp_mac_type_t interface);
void saveUserData(int index);
void loadUserData(int index);
bool setupWiFi(const char *newSSID, const char *newPassword);
String getUptime();

// Function to list all files and directories in the /recordings directory on the SD card in JSON format
String listRecordings() {
  DynamicJsonDocument doc(1024);
  JsonArray filesArray = doc.to<JsonArray>();

  Serial.println("Listing files stored in /recordings directory on SD card");

  File root = SD.open("/recordings");
  if (!root) {
    Serial.println("Failed to open /recordings directory");
    return "{\"error\": \"Failed to open /recordings directory\"}";
  }

  listDirContentsJson(root, "/recordings", filesArray);  // Call the recursive function
  root.close();

  String jsonString;
  serializeJson(doc, jsonString);
  Serial.println(jsonString);
  return jsonString;
}

// Recursive function to list directory contents in JSON format
void listDirContentsJson(File dir, String path, JsonArray &filesArray) {
  File file = dir.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      JsonObject dirObject = filesArray.createNestedObject();
      dirObject["type"] = "directory";
      dirObject["name"] = String(file.name());
      JsonArray subFilesArray = dirObject.createNestedArray("contents");
      listDirContentsJson(file, path + "/" + String(file.name()), subFilesArray);  // Recursively list the contents of the directory
    } else {
      JsonObject fileObject = filesArray.createNestedObject();
      fileObject["type"] = "file";
      fileObject["name"] = String(file.name());
      fileObject["size"] = file.size();
    }
    file = dir.openNextFile();
  }
}

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);     // Initialize the Arduino serial port
  espSerial.begin(115200);  // Initialize the ESP8266 serial port
  if (!initSDCard()) {
    Serial.println("An Error has occurred while initiating the SD Card");
    return;
  }
  if (!LittleFS.begin()) {
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  if (!AI.begin()) {
    Serial.println("Grove Vision AI not initiated");
  }

  esp32Info.chipID = (uint32_t)(ESP.getEfuseMac() >> 32);
  esp32Info.deviceModel = String(ESP.getChipModel()) + " Rev " + String(ESP.getChipRevision());
  esp32Info.chipCores = ESP.getChipCores();
  esp32Info.firmware = FIRMWAREVERSION;
  esp32Info.wificonnected = setupWiFi(ssid.c_str(), password.c_str());

  // Serve web app files from LittleFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/style.css", "text/css");
  });
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/script.js", "application/javascript");
  });
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/favicon.ico", "application/javascript");
  });

  server.on("/startStream", HTTP_GET, [](AsyncWebServerRequest *request) {
    streaming = true;
    request->send(200, "text/plain", "Stream started");
  });

  server.on("/stopStream", HTTP_GET, [](AsyncWebServerRequest *request) {
    streaming = false;
    request->send(200, "text/plain", "Stream stopped");
  });

  server.on("/lock", HTTP_GET, [](AsyncWebServerRequest *request) {
    lock = true;
    alert("Lock");
    request->send(200, "text/plain", "Locked");
  });
  server.on("/unlock", HTTP_GET, [](AsyncWebServerRequest *request) {
    lock = false;
    alert("Unlocked");
    request->send(200, "text/plain", "Unlocked");
  });

  server.on("/stream", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (lastImage.length() > 0) {
      String response = lastImage;
      request->send(200, "text/plain", response);
    } else {
      request->send(200, "text/plain", "data:,");  // Send an empty data URL to avoid errors
    }
  });

  server.on("/alerts", HTTP_GET, [](AsyncWebServerRequest *request) {
    char path[64];
    snprintf(path, sizeof(path), "/alerts/%04d-%02d.txt", year(), month());
    File file = SD.open(path, FILE_READ);
    if (file) {
      String alertContent = file.readString();
      request->send(200, "text/plain", alertContent);
      file.close();
    } else {
      request->send(500, "text/plain", "Failed to read file");
    }
  });

  // Endpoint to list recordings
  server.on("/list-recordings", HTTP_GET, [](AsyncWebServerRequest *request) {
    String files = listRecordings();
    request->send(200, "application/json", files);
  });

  // Endpoint to play a recording
  server.on("/play-recording", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("file")) {
        String filename = request->getParam("file")->value();
        File file = SD.open("/recordings/" + filename);
        if (file) {
            AsyncWebServerResponse *response = request->beginChunkedResponse("text/plain", 
                [file](uint8_t *buffer, size_t maxLen, size_t index) mutable -> size_t {
                    if (file.available()) {
                        size_t bytesRead = file.read(buffer, maxLen);
                        return bytesRead;
                    } else {
                        file.close();
                        return 0; // No more data to send
                    }
                });
            request->send(response);
        } else {
            request->send(404, "text/plain", "File not found");
        }
    } else {
        request->send(400, "text/plain", "Bad Request");
    }
});

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("file")) {
      String filePath = request->getParam("file")->value();
      if (SD.exists(filePath)) {
        String contentType = getContentType(filePath);
        request->send(SD, filePath.c_str(), contentType, true);
      } else {
        request->send(404, "text/plain", "File not found");
      }
    } else {
      request->send(400, "text/plain", "Filename not specified");
    }
  });

  server.on("/delete", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    if (request->hasParam("file")) {
      String filePath = request->getParam("file")->value();
      if (SD.remove(filePath.c_str())) {
        request->send(200, "text/plain", "File deleted successfully");
      } else {
        request->send(500, "text/plain", "Failed to delete file");
      }
    } else {
      request->send(400, "text/plain", "File not specified");
    }
  });

  server.on("/packages", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Rebooting ESP32...");
    delay(100);
    rebootESP("Requested from the Async Web Server");
  });

  server.on("/scan-networks", HTTP_GET, [](AsyncWebServerRequest *request) {
    int numNetworks = WiFi.scanNetworks();
    String response = "[";
    for (int i = 0; i < numNetworks; i++) {
      if (i > 0) response += ",";
      response += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"signal_strength\":" + String(WiFi.RSSI(i)) + "}";
    }
    response += "]";
    request->send(200, "application/json", response);
  });

  server.on("/connect", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid") && request->hasParam("password")) {
      String tempssid = request->getParam("ssid")->value();
      String temppassword = request->getParam("password")->value();
      Serial.println("Connecting to SSID: " + tempssid);

      if (setupWiFi(tempssid.c_str(), temppassword.c_str())) {  // Attempt to connect to the new Wi-Fi network
        esp32Info.wificonnected = true;
        request->send(200, "text/plain", "Connected to new network");
      } else {  // Failed to connect, try reconnecting to the previous network or set up an access point
        if (!setupWiFi(esp32Info.ssid.c_str(), esp32Info.wifi_password.c_str())) {
          WiFi.softAP(AP_ssid, AP_password);
          esp32Info.wificonnected = false;
          request->send(500, "text/plain", "Failed to connect to new network and access point set up.");
        } else {
          request->send(200, "text/plain", "Failed to connect to new network, reconnected to previous network.");
        }
      }
    } else {
      request->send(400, "text/plain", "Missing SSID or password");
    }
  });

  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
    String logs = listFiles(true);  // Pass true to get HTML-formatted output
    request->send(200, "text/html", logs);
  });

  server.on("/manage-users", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  // Route to get device info
  server.on("/device-info", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"deviceModel\":\"" + esp32Info.deviceModel + "\",";
    json += "\"chipID\":" + String(esp32Info.chipID) + ",";
    json += "\"chipCores\":" + String(esp32Info.chipCores) + ",";
    json += "\"MAC_Address\":\"" + esp32Info.MAC_Address + "\",";
    json += "\"firmware\":\"" + esp32Info.firmware + "\",";
    json += "\"ssid\":\"" + esp32Info.ssid + "\",";
    json += "\"wifi_password\":\"" + esp32Info.wifi_password + "\",";
    json += "\"signalstrength\":" + String(esp32Info.signalstrength) + ",";
    json += "\"hostname\":\"" + esp32Info.hostname + "\",";
    json += "\"IP\":\"" + esp32Info.IP.toString() + "\",";
    json += "\"wificonnected\":" + String(esp32Info.wificonnected ? "true" : "false") + ",";
    json += "\"uptime\":\"" + getUptime() + "\",";
    json += "\"gmtoffset\":" + String(esp32Info.gmtoffset) + ",";
    json += "\"daylightoffset\":" + String(esp32Info.daylightoffset) + ",";
    json += "\"interval\":" + String(esp32Info.interval);
    json += "}";
    request->send(200, "application/json", json);
  });

  WebSerial.begin(&server);
  ElegantOTA.begin(&server);
  WebSerial.onMessage([&](uint8_t *data, size_t len) {
    Serial.printf("Received %lu bytes from WebSerial: ", len);
    Serial.write(data, len);
    Serial.println();
    WebSerial.println("Received Data...");
    String d = "";
    for (size_t i = 0; i < len; i++) {
      d += char(data[i]);
    }
    WebSerial.println(d);
  });
  server.begin();
  Serial.print("Web Server Ready! Use 'http://");
  Serial.print(esp32Info.IP);
  Serial.println("' to connect");
}

void loop() {
  if (streaming)  // Streaming imgages
    while (streaming) {
      if (!AI.invoke(1, false, true)) {
        if (AI.last_image().length() > 0) {
          lastImage = AI.last_image();
          // Serial.println("Captured Image: " + lastImage);
        }
      }
    }
  else if (!AI.invoke()) {  // if sensor sees person then take snapshots for 30 seconds as according to recordGrove
    for (int i = 0; i < AI.boxes().size(); i++)
      if (AI.boxes()[i].score > 70) {
        alert("Person detected. 30sec recording to begin.");
        recordGrove();
      }
  }
  ElegantOTA.loop();
  WebSerial.loop();
}

void rebootESP(String message) {
  log("Rebooting ESP32: ");
  log(message.c_str());
  ESP.restart();
}

// Recursive function to list directory contents
void listDirContents(File dir, String path, String &returnText, bool ishtml) {
  File file = dir.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      String dirName = path + "/" + String(file.name());
      if (ishtml) {
        returnText += "<tr><td><b>Directory:</b> " + dirName + "</td><td></td><td></td><td></td></tr>";
      } else {
        returnText += "Directory: " + dirName + "\n";
      }
      listDirContents(file, dirName, returnText, ishtml);  // Recursively list the contents of the directory
    } else {
      if (ishtml) {
        returnText += "<tr align='left'><td>" + path + "/" + String(file.name()) + "</td><td>" + humanReadableSize(file.size()) + "</td>";
        returnText += "<td><button onclick=\"downloadDeleteButton('" + path + "/" + String(file.name()) + "', 'download')\">Download</button></td>";
        returnText += "<td><button onclick=\"downloadDeleteButton('" + path + "/" + String(file.name()) + "', 'delete')\">Delete</button></td></tr>";
      } else {
        returnText += "File: " + path + "/" + String(file.name()) + " Size: " + humanReadableSize(file.size()) + "\n";
      }
    }
    file = dir.openNextFile();
  }
}

// Function to list all files and directories on the SD card
String listFiles(bool ishtml) {
  String returnText = "";
  Serial.println("Listing files stored on SD card");

  File root = SD.open("/");
  if (ishtml) {
    returnText += "<table><tr><th align='left'>Name</th><th align='left'>Size</th><th></th><th></th></tr>";
  }
  listDirContents(root, "", returnText, ishtml);  // Call the recursive function
  if (ishtml) {
    returnText += "</table>";
    Serial.println(returnText);
  }
  root.close();

  return returnText;
}

// Make size of files human readable
String humanReadableSize(const size_t bytes) {
  if (bytes < 1024)
    return String(bytes) + " B";
  else if (bytes < (1024 * 1024))
    return String(bytes / 1024.0) + " KB";
  else if (bytes < (1024 * 1024 * 1024))
    return String(bytes / 1024.0 / 1024.0) + " MB";
  else
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}

String getContentType(String filename) {  // need to remove if not using
  if (filename.endsWith(".htm"))
    return "text/html";
  else if (filename.endsWith(".html"))
    return "text/html";
  else if (filename.endsWith(".css"))
    return "text/css";
  else if (filename.endsWith(".js"))
    return "application/javascript";
  else if (filename.endsWith(".png"))
    return "image/png";
  else if (filename.endsWith(".gif"))
    return "image/gif";
  else if (filename.endsWith(".jpg"))
    return "image/jpeg";
  else if (filename.endsWith(".ico"))
    return "image/x-icon";
  else if (filename.endsWith(".xml"))
    return "text/xml";
  else if (filename.endsWith(".pdf"))
    return "application/pdf";
  else if (filename.endsWith(".zip"))
    return "application/zip";
  else if (filename.endsWith(".gz"))
    return "application/x-gzip";
  else if (filename.endsWith(".txt"))
    return "text/plain";
  return "application/octet-stream";  // Default binary type
}

bool initSDCard() {
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("Card Mount Failed");
    return 0;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return 0;
  }
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  return 1;
}

void writeFile(fs::FS &fs, const char *path, const char *message) {
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("Failed to open file for writing: %s\n", path);
    return;
  }
  if (file.print(message)) {
    // Serial.println("File written");
  } else {
    // Serial.println("Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char *path, const char *message) {
  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.printf("Failed to open file for appedning: %s\n", path);
    return;
  }
  if (file.print(message)) {
    // Serial.println("Message appended");
  } else {
    // Serial.println("Append failed");
  }
  file.close();
}

void recordGrove() {
  previousMillis = millis();
  unsigned long currentMillis = previousMillis;
  struct tm timeinfo;
  char filename[40];
  char timestamp[32];
  getLocalTime(&timeinfo);
  snprintf(filename, sizeof(filename), "/recordings/%04d-%02d-%02d-%02d-%02d-%02d.txt", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  writeFile(SD, filename, "Initial log\n");
  String dataToAppend = "";  // Initialize an empty string to accumulate data

  while (millis() - previousMillis < esp32Info.interval) {
    if (!AI.invoke(1, false, true))
      if (AI.last_image().length() > 0) {
        dataToAppend = AI.last_image() + "\n";  // Accumulate image data
        lastImage = dataToAppend;
        appendFile(SD, filename, dataToAppend.c_str());
      }
    currentMillis = millis();
  }
}

// Function to append a message to an alert file
void alert(const char *message) {
  char path[64];
  snprintf(path, sizeof(path), "/alerts/%04d-%02d.txt", year(), month());
  createDirectories(path);
  Serial.printf("Appending to alert file: %s\n", path);

  File file = SD.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open alert file for appending");
    return;
  }
  char timestamp[32];
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d ", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    file.print(timestamp);
  }

  String messageWithNewline = String(message) + "\n";
  if (file.print(messageWithNewline))
    Serial.println("Alert message appended");
  else
    Serial.println("Append to alert file failed");
  file.close();
}

// Function to append a message to a log file
void log(const char *message, bool serial) {
  if (serial)
    Serial.println(message);
  char path[64];
  snprintf(path, sizeof(path), "/device/logs/%04d-%02d.txt", year(), month());
  createDirectories(path);
  Serial.printf("Appending to log file: %s\n", path);

  File file = SD.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open log file for appending");
    return;
  }

  char timestamp[32];
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d ", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    file.print(timestamp);
  }

  String messageWithNewline = String(message) + "\n";
  if (file.print(messageWithNewline))
    Serial.println("Log message appended");
  else
    Serial.println("Append to log file failed");
  file.close();
}

int year() {  // Helper functions to get the current year
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    return timeinfo.tm_year + 1900;
  }
  return 1970;  // Default year if time is not available
}

int month() {  // Helper functions to get the current month
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    return timeinfo.tm_mon + 1;
  }
  return 1;  // Default month if time is not available
}

void createDirectories(const char *path) {  // Function to create directories if they do not exist
  char temp[64];
  char *pos = temp;
  snprintf(temp, sizeof(temp), "%s", path);

  for (pos = temp + 1; *pos; pos++) {
    if (*pos == '/') {
      *pos = '\0';
      SD.mkdir(temp);
      *pos = '/';
    }
  }
}

void setTimezone() {
  setenv("TZ", "America/Chicago", 1);
  tzset();
}

String getInterfaceMacAddress(esp_mac_type_t interface) {
  String mac = "";
  unsigned char mac_base[6] = { 0 };
  if (esp_read_mac(mac_base, interface) == ESP_OK) {
    char buffer[18];  // 6*2 characters for hex + 5 characters for colons + 1 character for null terminator
    sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X", mac_base[0], mac_base[1], mac_base[2], mac_base[3], mac_base[4], mac_base[5]);
    mac = buffer;
  }
  return mac;
}

void saveUserData(int index) {
  userdata.begin("user-data", false);  // Open Preferences with "user-data" namespace in RW mode

  // Create unique keys for each field
  String usernameKey = "username" + String(index);
  String passwordKey = "password" + String(index);
  String emailKey = "email" + String(index);
  String phoneNumberKey = "phonenumber" + String(index);
  String pinKey = "pin" + String(index);
  String adminKey = "admin" + String(index);

  // Save user data with unique keys
  userdata.putString(usernameKey.c_str(), userid[index].username);
  userdata.putString(passwordKey.c_str(), userid[index].password);
  userdata.putString(emailKey.c_str(), userid[index].email);
  userdata.putString(phoneNumberKey.c_str(), userid[index].phonenumber);
  userdata.putBytes(pinKey.c_str(), userid[index].pin, sizeof(userid[index].pin));
  userdata.putBool(adminKey.c_str(), userid[index].admin);
  userdata.end();
}

void loadUserData(int index) {
  userdata.begin("user-data", true);  // Open Preferences with "user-data" namespace in RO mode

  // Create unique keys for each field
  String usernameKey = "username" + String(index);
  String passwordKey = "password" + String(index);
  String emailKey = "email" + String(index);
  String phoneNumberKey = "phonenumber" + String(index);
  String pinKey = "pin" + String(index);
  String adminKey = "admin" + String(index);

  // Load user data with unique keys
  userid[index].username = userdata.getString(usernameKey.c_str(), "");
  userid[index].password = userdata.getString(passwordKey.c_str(), "");
  userid[index].email = userdata.getString(emailKey.c_str(), "");
  userid[index].phonenumber = userdata.getString(phoneNumberKey.c_str(), "");
  userdata.getBytes(pinKey.c_str(), userid[index].pin, sizeof(userid[index].pin));
  userid[index].admin = userdata.getBool(adminKey.c_str(), false);

  userdata.end();
}

bool setupWiFi(const char *newSSID, const char *newPassword) {
  bool connected = false;
  int attempts;

  if (WiFi.getMode() == WIFI_AP) {  // Switch from AP mode to STA mode if necessary
    Serial.println("Currently in AP mode. Switching to STA mode...");
    WiFi.softAPdisconnect(true);
    delay(3000);
  }

  auto attemptConnection = [](const char *ssid, const char *password) {  // Function to attempt connection to a given Wi-Fi network
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setHostname(host);
      esp32Info.ssid = ssid;
      esp32Info.wifi_password = password;
      esp32Info.signalstrength = WiFi.RSSI();
      esp32Info.IP = WiFi.localIP();
      esp32Info.MAC_Address = getInterfaceMacAddress(ESP_MAC_WIFI_STA);
      esp32Info.hostname = WiFi.getHostname();
      esp32Info.signalstrength = WiFi.RSSI();
      setTimezone();
      configTime(esp32Info.gmtoffset, esp32Info.daylightoffset, "pool.ntp.org");
      return true;
    } else return false;
  };

  if (WiFi.status() == WL_CONNECTED) {  // If already connected, disconnect and try new network
    log("Currently connected to Wi-Fi. Disconnecting...");
    WiFi.disconnect();
    delay(3000);
    if (attemptConnection(newSSID, newPassword)) {
      log("Connected to new Wi-Fi network.");
      return true;
    } else {  // Retrieve previous network credentials
      log("Failed to connect to new Wi-Fi network. Trying to reconnect to previous network...");
      String oldSSID = WiFi.SSID();
      String oldPassword = WiFi.psk();
      if (attemptConnection(oldSSID.c_str(), oldPassword.c_str())) {
        log("Reconnected to previous Wi-Fi network.");
        return true;
      } else {
        log("Failed to reconnect to previous Wi-Fi network.");
      }
    }
  } else {  // No current connection, attempt to connect to new Wi-Fi network
    if (attemptConnection(newSSID, newPassword)) {
      log("Connected to new Wi-Fi network.");
      return true;
    } else {
      log("Failed to connect to new Wi-Fi network.");
    }
  }

  if (WiFi.softAP(AP_ssid, AP_password)) {  // Setup Access Point if connection attempts failed
    WiFi.softAPsetHostname(host);
    esp32Info.IP = WiFi.softAPIP();
    esp32Info.MAC_Address = getInterfaceMacAddress(ESP_MAC_WIFI_SOFTAP);
    esp32Info.hostname = WiFi.softAPgetHostname();
    log("Access Point setup with SSID: ");
    log(AP_ssid);
  }
  return false;
}

String getUptime() {
  unsigned long millisec = millis();
  // Calculate days, hours, minutes, and seconds
  unsigned long seconds = millisec / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;

  // Remainders for hours, minutes, and seconds
  seconds = seconds % 60;
  minutes = minutes % 60;
  hours = hours % 24;

  char uptimeStr[50];
  snprintf(uptimeStr, sizeof(uptimeStr), "%lud %luh %lum %lus", days, hours, minutes, seconds);
  return esp32Info.uptime = String(uptimeStr);
}