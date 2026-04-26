//
// Created by Aram Aprahamian on 8/2/25.
//
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <time.h>


#include "env.h" // API keys

#ifdef ARDUINO_UNOR4_WIFI
  #include <Arduino_LED_Matrix.h>
  ArduinoLEDMatrix matrix;
  #define HAS_MATRIX 1
  uint8_t allOnBitmap[12][8] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
  };
#else
  #define HAS_MATRIX 0
#endif

String apiKey = OPENWEATHER_API_KEY;
String units             = "imperial";
String language          = "en";
#define DAC_PIN           5
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
const int MODE_BUTTON_PIN   = 2;
const int ADJUST_BUTTON_PIN = 3;

// EEPROM Layout (as before)
#define EEPROM_VALID_ADDR         0
#define EEPROM_ALARM_HOUR_ADDR    1
#define EEPROM_ALARM_MINUTE_ADDR  2
#define EEPROM_ALARM_ENABLED_ADDR 3
#define EEPROM_BEEP_ENABLED_ADDR  4
#define EEPROM_TIMEFORMAT_ADDR    5
#define EEPROM_SSID_ADDR          6
#define EEPROM_PWD_ADDR           38
#define EEPROM_CITY_ADDR          70
#define EEPROM_COUNTRY_ADDR       102
#define EEPROM_TZ_ADDR            110
#define EEPROM_LED_MATRIX_ADDR    118
#define EEPROM_LAST_TIME_HOUR     119
#define EEPROM_LAST_TIME_MINUTE   120
#define EEPROM_LAST_TIME_SECOND   121
#define EEPROM_LAST_TIME_DAY      122
#define EEPROM_LAST_TIME_MONTH    123
#define EEPROM_LAST_TIME_YEAR     124
#define EEPROM_VALID_VALUE        0xAB

// WiFi
char wifiSSID[33];
char wifiPWD[33];
bool beepEnabled         = true;
bool use24hr             = false;
bool ledMatrixEnabled    = false;

// Static IP configuration
IPAddress staticIP(192, 168, 50, 113);
IPAddress gateway(192, 168, 50, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8); // Google DNS
IPAddress dns2(1, 1, 1, 1); // Cloudflare DNS as backup
bool useStaticIP = true;

// Alarm
int  alarmHour       = 7;
int  alarmMinute     = 0;
bool alarmEnabled    = false;
bool alarmSounding   = false;

// Non-blocking alarm
unsigned long alarmStartTime = 0;
unsigned long lastBeepTime = 0;
const unsigned long alarmDuration = 300000; // 5 minutes 
const unsigned long beepInterval = 250;    // ms
const unsigned long beepLength = 150;      // ms
bool beepActive = false;

// Time
bool timeHasBeenSet        = false;
long localTimeOffsetInSecs = 0;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 10 * 1000); // NTP update every 10 seconds

// Multiple NTP servers for redundancy
const char* ntpServers[] = {
  "pool.ntp.org",
  "time.nist.gov", 
  "time.google.com",
  "time.windows.com",
  "time.cloudflare.com",
  "time.apple.com"
};
int currentNtpServer = 0;
const int numNtpServers = 6;

// Fallback time when NTP fails
unsigned long fallbackStartTime = 0;
bool usingFallbackTime = false;

// NTP retry logic
unsigned long lastNTPRetry = 0;
const unsigned long ntpRetryInterval = 30000; // Retry NTP every 30 seconds if failed
int ntpRetryCount = 0;
const int maxNTPRetries = 10;

// Enhanced DNS and network diagnostics
bool networkDiagnosticsEnabled = true;
unsigned long lastNetworkCheck = 0;
const unsigned long networkCheckInterval = 60000; // Check network every minute

// Display modes
enum DisplayMode {
  SHOW_MAIN,
  SET_ALARM_HOUR,
  SET_ALARM_MINUTE,
  SHOW_ALARM_STATUS,
  MENU_BEEP,
  MENU_TIMEFORMAT,
  MENU_LED
};
DisplayMode currentMode = SHOW_MAIN;

// Weather/location
String cityQuery = "Los Angeles,US";

// Alarm logic
static bool alarmTriggeredThisMinute = false;
static int lastCheckedMinute = -1;

// Invert display during alarm
unsigned long lastInvertToggle = 0;
bool invertState = false;
const unsigned long invertInterval = 250; // ms

// Timers
unsigned long lastWeatherUpdate = 0;
const unsigned long weatherInterval = 240000; // 4 min
unsigned long lastNTPUpdate = 0;
const unsigned long ntpUpdateInterval = 10 * 1000; // 10 seconds

// Button logic
unsigned long lastModeButtonPress   = 0;
unsigned long lastAdjustButtonPress = 0;
bool          adjustButtonIsPressed = false;
unsigned long adjustPressStartTime  = 0;
const long    debounceDelay         = 250;
const long    adjust5Delay          = 1500;
bool          autoIncrementBy5      = false;
unsigned long lastAutoIncrementTime = 0;

// Display control
bool displayOn = true;
unsigned long modeButtonPressStart = 0;
const unsigned long displayOffDelay = 3000; // 5 seconds
bool displayJustTurnedOff = false; // Flag to prevent immediate turn-on
unsigned long displayTurnOffTime = 0; // When display was turned off

// Misc
int  tempAlarmHour   = 7;
int  tempAlarmMinute = 0;

// XOR encryption key for password
const char XOR_KEY = 0x5A;



// --- Web Server ---
WiFiServer server(80);

// --- EEPROM Utility Functions ---
void writeStringToEEPROM(int addr, const String& str, int maxLen) {
  int i = 0;
  for (; i < str.length() && i < maxLen - 1; ++i)
    EEPROM.update(addr + i, str[i]);
  EEPROM.update(addr + i, '\0'); // null terminate
}

String readStringFromEEPROM(int addr, int maxLen) {
  char buf[maxLen];
  for (int i = 0; i < maxLen; ++i) {
    buf[i] = EEPROM.read(addr + i);
    if (buf[i] == '\0') break;
  }
  buf[maxLen - 1] = '\0';
  return String(buf);
}

// XOR encrypt/decrypt password
void writeEncryptedPasswordToEEPROM(int addr, const String& pwd, int maxLen) {
  int i = 0;
  for (; i < pwd.length() && i < maxLen - 1; ++i) {
    EEPROM.update(addr + i, pwd[i] ^ XOR_KEY);
  }
  EEPROM.update(addr + i, '\0');
}

String readDecryptedPasswordFromEEPROM(int addr, int maxLen) {
  char buf[maxLen];
  for (int i = 0; i < maxLen; ++i) {
    char c = EEPROM.read(addr + i);
    if (c == '\0') { buf[i] = '\0'; break; }
    buf[i] = c ^ XOR_KEY;
  }
  buf[maxLen - 1] = '\0';
  return String(buf);
}

// --- Location EEPROM Functions ---
void saveLocationToEEPROM(const String& city, const String& country, const String& timezone) {
  writeStringToEEPROM(EEPROM_CITY_ADDR, city, 32);
  writeStringToEEPROM(EEPROM_COUNTRY_ADDR, country, 8);
  writeStringToEEPROM(EEPROM_TZ_ADDR, timezone, 8);
}

void loadLocationFromEEPROM(String& city, String& country, String& timezone) {
  city = readStringFromEEPROM(EEPROM_CITY_ADDR, 32);
  country = readStringFromEEPROM(EEPROM_COUNTRY_ADDR, 8);
  timezone = readStringFromEEPROM(EEPROM_TZ_ADDR, 8);
}

// --- WiFi Credential Functions ---
void promptWiFiCredentials() {
  Serial.println(F("Enter WiFi SSID:"));
  String ssid = "";
  while (ssid.length() == 0) {
    while (Serial.available() == 0) delay(10);
    ssid = Serial.readStringUntil('\n');
    ssid.trim();
  }
  Serial.println(F("Enter WiFi Password:"));
  String password = "";
  while (password.length() == 0) {
    while (Serial.available() == 0) delay(10);
    password = Serial.readStringUntil('\n');
    password.trim();
  }
  writeStringToEEPROM(EEPROM_SSID_ADDR, ssid, 32);
  writeEncryptedPasswordToEEPROM(EEPROM_PWD_ADDR, password, 32);
  EEPROM.update(EEPROM_VALID_ADDR, EEPROM_VALID_VALUE); // mark as valid
  Serial.println(F("WiFi credentials saved. Rebooting..."));
  delay(500);
  NVIC_SystemReset(); // For ARM (UNO R4); for AVR use asm volatile ("jmp 0");
}

// --- Should Call IP2Location? ---
bool shouldCallIP2Location(const String& currentSSID) {
  String savedSSID = readStringFromEEPROM(EEPROM_SSID_ADDR, 32);
  String city = readStringFromEEPROM(EEPROM_CITY_ADDR, 32);
  String country = readStringFromEEPROM(EEPROM_COUNTRY_ADDR, 8);
  String timezone = readStringFromEEPROM(EEPROM_TZ_ADDR, 8);
  if (savedSSID != currentSSID) return true;
  if (city.length() == 0 || country.length() == 0 || timezone.length() == 0) return true;
  return false;
}

// --- EEPROM Reset Command ---
void resetEEPROM() {
  EEPROM.update(EEPROM_VALID_ADDR, 0);
  for (int i = EEPROM_SSID_ADDR; i < EEPROM_SSID_ADDR + 32; ++i) EEPROM.update(i, 0);
  for (int i = EEPROM_PWD_ADDR; i < EEPROM_PWD_ADDR + 32; ++i) EEPROM.update(i, 0);
  for (int i = EEPROM_CITY_ADDR; i < EEPROM_CITY_ADDR + 32; ++i) EEPROM.update(i, 0);
  for (int i = EEPROM_COUNTRY_ADDR; i < EEPROM_COUNTRY_ADDR + 8; ++i) EEPROM.update(i, 0);
  for (int i = EEPROM_TZ_ADDR; i < EEPROM_TZ_ADDR + 8; ++i) EEPROM.update(i, 0);
  EEPROM.update(EEPROM_LED_MATRIX_ADDR, 0);
  Serial.println(F("EEPROM WiFi/location data erased. Rebooting..."));
  delay(500);
  NVIC_SystemReset();
}

// --- Weather State Management ---
enum WeatherState {
  WEATHER_IDLE,
  WEATHER_FETCHING,
  WEATHER_SUCCESS,
  WEATHER_FAILED
};

// Enhanced weather data structure for One Call API 3.0
struct DetailedWeatherData {
  String city;
  String temp;
  String condition;
  String description;
  int humidity;
  float windSpeed;
  String windDirection;
  int pressure;
  float tempMin;
  float tempMax;
  int sunrise;
  int sunset;
  float feelsLike;
  int visibility;
  float dewPoint;
  int clouds;
  bool dirty;
  WeatherState state;
  unsigned long lastUpdate;
  unsigned long lastAttempt;
  int retryCount;
  
  // Hourly forecast data (next 24 hours)
  struct HourlyForecast {
    int hour;
    float temp;
    String condition;
    int humidity;
    float windSpeed;
    int clouds;
  };
  HourlyForecast hourly[24];
  int hourlyCount;
};

DetailedWeatherData detailedWeatherData = {
  "???", "N/A", "N/A", "N/A", 0, 0.0, "N/A", 0, 0.0, 0.0, 0, 0, 0.0, 0, 0.0, 0,
  true, WEATHER_IDLE, 0, 0, 0, {}, 0
};

// Helper function to convert wind degrees to direction
String getWindDirection(int degrees) {
  if(degrees >= 337.5 || degrees < 22.5) return "N";
  if(degrees >= 22.5 && degrees < 67.5) return "NE";
  if(degrees >= 67.5 && degrees < 112.5) return "E";
  if(degrees >= 112.5 && degrees < 157.5) return "SE";
  if(degrees >= 157.5 && degrees < 202.5) return "S";
  if(degrees >= 202.5 && degrees < 247.5) return "SW";
  if(degrees >= 247.5 && degrees < 292.5) return "W";
  if(degrees >= 292.5 && degrees < 337.5) return "NW";
  return "N";
}

// --- Alarm/Clock/Weather Prototypes ---
static void zeroPad2(char* buf, int val){ sprintf(buf, "%02d", val); }
void beepDAC(uint16_t level, unsigned long durationMs, bool force = false);
void beepOnButtonPress();
bool modeButtonPressed();
bool adjustButtonPressed();
void handleButtons();
void checkAlarm();
void soundAlarm();
void stopAlarm();
void waitForButtonRelease();
void saveAlarmToEEPROM();
void loadAlarmFromEEPROM();
void updateDisplay();
void drawTimeTop();
void drawAlarmTopRight();
void drawPaneWeather(const String& temp, const String& city, const String& condition);
void drawProgressBar(int progress, int total, const String& message);
void fetchIP2LocationAndSetOffset();
void parseTimezoneAndSetOffset(const String& tzString);
void initNTP();
bool checkDNSResolution();
bool updateLocalTime();

void performNetworkDiagnostics() {
  Serial.println(F("=== Network Diagnostics ==="));
  
  // WiFi status
  Serial.print(F("WiFi Status: "));
  int wifiStatus = WiFi.status();
  switch(wifiStatus) {
    case WL_CONNECTED:
      Serial.println(F("CONNECTED"));
      Serial.print(F("SSID: ")); Serial.println(WiFi.SSID());
      Serial.print(F("IP Address: ")); Serial.println(WiFi.localIP());
      Serial.print(F("Gateway: ")); Serial.println(WiFi.gatewayIP());
      Serial.print(F("Subnet: ")); Serial.println(WiFi.subnetMask());
      Serial.print(F("DNS: ")); Serial.println(WiFi.dnsIP());
      Serial.print(F("RSSI: ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
      break;
    case WL_NO_SSID_AVAIL:
      Serial.println(F("NO_SSID_AVAIL"));
      break;
    case WL_CONNECT_FAILED:
      Serial.println(F("CONNECT_FAILED"));
      break;
    case WL_IDLE_STATUS:
      Serial.println(F("IDLE_STATUS"));
      break;
    case WL_DISCONNECTED:
      Serial.println(F("DISCONNECTED"));
      break;
    default:
      Serial.print(F("UNKNOWN ("));
      Serial.print(wifiStatus);
      Serial.println(F(")"));
      break;
  }
  
  // DNS resolution test
  Serial.print(F("DNS Resolution Test: "));
  if (checkDNSResolution()) {
    Serial.println(F("PASS"));
  } else {
    Serial.println(F("FAIL"));
  }
  
  // Time sync status
  Serial.print(F("Time Sync Status: "));
  if (timeHasBeenSet) {
    Serial.println(F("SYNCED"));
    Serial.print(F("Current Time: "));
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char timeStr[9];
    sprintf(timeStr, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    Serial.println(timeStr);
  } else {
    Serial.println(F("NOT SYNCED"));
    if (usingFallbackTime) {
      Serial.println(F("Using fallback time"));
    }
  }
  
  // Weather API status
  Serial.print(F("Weather API Status: "));
  switch(detailedWeatherData.state) {
    case WEATHER_IDLE:
      Serial.println(F("IDLE"));
      break;
    case WEATHER_FETCHING:
      Serial.println(F("FETCHING"));
      break;
    case WEATHER_SUCCESS:
      Serial.println(F("SUCCESS"));
      Serial.print(F("Last Update: ")); Serial.println(detailedWeatherData.lastUpdate);
      break;
    case WEATHER_FAILED:
      Serial.println(F("FAILED"));
      Serial.print(F("Retry Count: ")); Serial.println(detailedWeatherData.retryCount);
      break;
  }
  
  Serial.println(F("=== End Diagnostics ==="));
}
void saveLastKnownTime();
void loadLastKnownTime();
void handleWebRequests(); // New prototype for web server

// --- Non-blocking Functions ---
bool fetchWeatherNonBlocking();
bool tryOneCallAPI(WiFiClient& client);
bool tryBasicWeatherAPI(WiFiClient& client);

bool updateLocalTimeNonBlocking();
void requestWeatherUpdate();
void requestNTPUpdate();
void getWeatherData(String& city, String& temp, String& condition, bool& dirty);
void getDetailedWeatherData(DetailedWeatherData& data);
void processWeatherState();
void processNTPState();
void processWebRequests();
void processWebRequest(const String& request, const String& postBody, WiFiClient& client);

// Non-blocking state management
bool weatherRequested = false;
bool ntpRequested = false;
unsigned long lastWeatherProcess = 0;
unsigned long lastNTPProcess = 0;
const unsigned long PROCESS_INTERVAL = 50; // Process every 50ms



// Weather fetch timeout
const unsigned long WEATHER_TIMEOUT = 15000; // 15 seconds
const unsigned long WEATHER_RETRY_INTERVAL = 60000; // 1 minute
const int MAX_WEATHER_RETRIES = 3;

// --- Weather State Processing ---
void processWeatherState() {
  if(weatherRequested && detailedWeatherData.state == WEATHER_IDLE) {
    detailedWeatherData.state = WEATHER_FETCHING;
    detailedWeatherData.lastAttempt = millis();
    weatherRequested = false;
    Serial.println("Weather state: IDLE -> FETCHING");
  }
  
  if(detailedWeatherData.state == WEATHER_FETCHING) {
    // Perform non-blocking weather fetch
    bool success = fetchWeatherNonBlocking();
    
    if(success) {
      detailedWeatherData.state = WEATHER_SUCCESS;
      detailedWeatherData.retryCount = 0;
      detailedWeatherData.lastUpdate = millis();
      lastWeatherUpdate = millis(); // Synchronize the global timer
      Serial.println("Weather state: FETCHING -> SUCCESS");
    } else {
      detailedWeatherData.state = WEATHER_FAILED;
      detailedWeatherData.retryCount++;
      Serial.println("Weather state: FETCHING -> FAILED");
    }
    detailedWeatherData.dirty = true;
  }
}

// --- NTP State Processing ---
void processNTPState() {
  if(ntpRequested) {
    // Perform non-blocking NTP update
    bool success = updateLocalTimeNonBlocking();
    
    if(success) {
      timeHasBeenSet = true;
      ntpRetryCount = 0;
      usingFallbackTime = false;
      saveLastKnownTime();
    } else {
      ntpRetryCount++;
      if(!usingFallbackTime) {
        fallbackStartTime = millis();
        usingFallbackTime = true;
      }
    }
    
    ntpRequested = false;
  }
}

// --- Simple Non-blocking Web Server ---
void processWebRequests() {
  WiFiClient client = server.available();
  if (client) {
    // Simple approach - just handle one request at a time
    String request = "";
    String postBody = "";
    unsigned long startTime = millis();
    const unsigned long timeout = 3000; // 3 second timeout
    bool headersComplete = false;
    
    // Read headers and body with timeout
    while (client.connected() && (millis() - startTime) < timeout) {
      if (client.available()) {
        char c = client.read();
        request += c;
        
        // Check for end of headers
        if (!headersComplete && request.indexOf("\r\n\r\n") >= 0) {
          headersComplete = true;
          
          // Check if this is a POST request
          if (request.indexOf("POST") >= 0) {
            // Extract Content-Length
            int clPos = request.indexOf("Content-Length: ");
            if (clPos >= 0) {
              int clEnd = request.indexOf("\r\n", clPos);
              String clStr = request.substring(clPos + 16, clEnd);
              int contentLength = clStr.toInt();
              
              // Read POST body
              if (contentLength > 0) {
                unsigned long bodyStart = millis();
                while (client.connected() && (millis() - bodyStart) < 2000 && postBody.length() < contentLength) {
                  if (client.available()) {
                    postBody += (char)client.read();
                  } else {
                    delay(1);
                  }
                }
              }
            }
          }
          break; // Headers complete, process request
        }
      } else {
        delay(1); // Small delay to prevent blocking
      }
    }
    
    // Process the request
    if (headersComplete) {
      processWebRequest(request, postBody, client);
    }
    
    client.stop();
  }
}

// --- Non-blocking Weather Fetch (Basic API Only) ---
bool fetchWeatherNonBlocking() {
  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return false;
  }
  
  Serial.println("Starting weather fetch...");
  WiFiClient client;
  
  // Skip One Call API - too large for Arduino Uno R4 (32KB RAM)
  // One Call API returns ~8191 bytes which causes stack overflow
  Serial.println("Skipping One Call API (too large for Arduino Uno R4)");
  
  // Use basic weather API only (free tier, ~500 bytes)
  Serial.println("Using basic weather API...");
  bool basicSuccess = tryBasicWeatherAPI(client);
  if(basicSuccess) {
    Serial.println("Basic weather API successful");
  } else {
    Serial.println("Basic weather API failed");
  }
  return basicSuccess;
}

bool tryOneCallAPI(WiFiClient& client) {
  // First, get coordinates for the city using Geocoding API
  String q = cityQuery; 
  q.replace(" ", "%20");
  String host = "api.openweathermap.org";
  String geocodePath = "/geo/1.0/direct?q=" + q + "&limit=1&appid=" + apiKey;
  
  Serial.print("Fetching coordinates: "); 
  Serial.print(host); 
  Serial.println(geocodePath);
  
  if(!client.connect(host.c_str(), 80)) {
    Serial.println("Geocoding connect fail");
    return false;
  }
  
  client.print("GET " + geocodePath + " HTTP/1.1\r\n"
               "Host: " + host + "\r\n"
               "User-Agent: Arduino\r\n"
               "Connection: close\r\n\r\n");
  
  unsigned long startTime = millis();
  
  while(client.connected() && (millis() - startTime) < WEATHER_TIMEOUT) {
    if(client.available()) {
      String line = client.readStringUntil('\n');
      if(line.length() <= 1) { // Empty line = headers done
        Serial.println("Geocoding headers complete, reading JSON...");
        break;
      }
    } else {
      delay(1);
    }
  }
  
  // Read JSON response with pre-allocated buffer
  char responseBuffer[8192];
  int totalBytes = 0;
  
  // Wait a moment for data to be available
  delay(50);
  
  // Read all available data with timeout
  unsigned long readStartTime = millis();
  while(client.connected() && (millis() - readStartTime) < 5000) { // 5 second timeout
    if(client.available()) {
      int bytesToRead = min(client.available(), (int)(sizeof(responseBuffer) - totalBytes - 1));
      if(bytesToRead > 0) {
        int bytesRead = client.readBytes(responseBuffer + totalBytes, bytesToRead);
        totalBytes += bytesRead;
        readStartTime = millis(); // Reset timeout on successful read
      } else {
        delay(1);
      }
    } else {
      delay(10);
      // If no data for 100ms, assume we're done
      if(millis() - readStartTime > 100) {
        break;
      }
    }
  }
  
  responseBuffer[totalBytes] = '\0';
  client.stop();
  
  if(totalBytes == 0) {
    Serial.println("No geocoding response data received");
    return false;
  }
  
  Serial.print("Geocoding response received: "); 
  Serial.print(totalBytes); 
  Serial.println(" bytes");
  
  // Debug: Print first 200 characters of response
  Serial.print("Response preview: ");
  String response(responseBuffer);
  if(response.length() > 200) {
    Serial.println(response.substring(0, 200));
  } else {
    Serial.println(response);
  }
  
  // Parse JSON (single string creation)
  DynamicJsonDocument geoDoc(512);
  DeserializationError err = deserializeJson(geoDoc, response);
  if(err) {
    Serial.print("Geocoding parse error: "); 
    Serial.println(err.f_str());
    return false;
  }
  
  // Extract coordinates
  if(!geoDoc.is<JsonArray>() || geoDoc.size() == 0) {
    Serial.println("No geocoding results");
    return false;
  }
  
  float lat = geoDoc[0]["lat"] | 0.0;
  float lon = geoDoc[0]["lon"] | 0.0;
  String cityName = geoDoc[0]["name"] | "???";
  
  if(lat == 0.0 || lon == 0.0) {
    Serial.println("Invalid coordinates");
    return false;
  }
  
  Serial.print("Coordinates: "); Serial.print(lat); Serial.print(", "); Serial.println(lon);
  
  // Now fetch weather data using One Call API 3.0
  String weatherPath = "/data/3.0/onecall?lat=" + String(lat, 6) + "&lon=" + String(lon, 6) + 
                       "&exclude=minutely,daily,alerts&units=" + units + "&lang=" + language + "&appid=" + apiKey;
  
  Serial.print("Fetching One Call API weather: "); 
  Serial.print(host); 
  Serial.println(weatherPath);
  
  if(!client.connect(host.c_str(), 80)) {
    Serial.println("One Call API connect fail");
    return false;
  }
  
  client.print("GET " + weatherPath + " HTTP/1.1\r\n"
               "Host: " + host + "\r\n"
               "User-Agent: Arduino\r\n"
               "Connection: close\r\n\r\n");
  
  // FIXED: Efficient response reading
  startTime = millis();
  
  // Skip headers efficiently
  while(client.connected() && (millis() - startTime) < WEATHER_TIMEOUT) {
    if(client.available()) {
      String line = client.readStringUntil('\n');
      if(line.length() <= 1) { // Empty line = headers done
        Serial.println("One Call API headers complete, reading JSON...");
        break;
      }
    } else {
      delay(1);
    }
  }
  
  // Read JSON response with pre-allocated buffer
  totalBytes = 0;
  
  // Wait a moment for data to be available
  delay(50);
  
  // Read all available data with timeout
  unsigned long readStartTime2 = millis();
  while(client.connected() && (millis() - readStartTime2) < 5000) { // 5 second timeout
    if(client.available()) {
      int bytesToRead = min(client.available(), (int)(sizeof(responseBuffer) - totalBytes - 1));
      if(bytesToRead > 0) {
        int bytesRead = client.readBytes(responseBuffer + totalBytes, bytesToRead);
        totalBytes += bytesRead;
        readStartTime = millis(); // Reset timeout on successful read
      } else {
        delay(1);
      }
    } else {
      delay(10);
      // If no data for 100ms, assume we're done
      if(millis() - readStartTime > 100) {
        break;
      }
    }
  }
  
  responseBuffer[totalBytes] = '\0';
  client.stop();
  
  if(totalBytes == 0) {
    Serial.println("No One Call API response data received");
    return false;
  }
  
  Serial.print("One Call API response received: "); 
  Serial.print(totalBytes); 
  Serial.println(" bytes");
  
  // Parse JSON (single string creation)
  response = String(responseBuffer);
  DynamicJsonDocument doc(4096); // Increased size for One Call API
  err = deserializeJson(doc, response);
  if(err) {
    Serial.print("One Call API parse error: "); 
    Serial.println(err.f_str());
    return false;
  }
  
  // Extract current weather data
  JsonObject current = doc["current"];
  if(current.isNull()) {
    Serial.println("No current weather data in One Call API response");
    return false;
  }
  
  float temp = current["temp"] | 0.0;
  int humidity = current["humidity"] | 0;
  float windSpeed = current["wind_speed"] | 0.0;
  int pressure = current["pressure"] | 0;
  float feelsLike = current["feels_like"] | 0.0;
  int visibility = current["visibility"] | 0;
  float dewPoint = current["dew_point"] | 0.0;
  int clouds = current["clouds"] | 0;
  int sunrise = current["sunrise"] | 0;
  int sunset = current["sunset"] | 0;
  
  // Extract weather condition
  JsonArray weather = current["weather"];
  String condition = "Unknown";
  String description = "";
  if(weather.size() > 0) {
    condition = weather[0]["main"] | "Unknown";
    description = weather[0]["description"] | "";
  }
  
  // Extract wind direction
  int windDeg = current["wind_deg"] | 0;
  String windDirection = getWindDirection(windDeg);
  
  // Extract daily forecast (includes high/low temperatures)
  JsonArray daily = doc["daily"];
  if(daily.size() > 0) {
    JsonObject today = daily[0]; // First day is today
    detailedWeatherData.tempMin = today["temp"]["min"] | temp; // Daily low
    detailedWeatherData.tempMax = today["temp"]["max"] | temp; // Daily high
    Serial.print("Daily forecast - Low: "); Serial.print(detailedWeatherData.tempMin);
    Serial.print("°F, High: "); Serial.print(detailedWeatherData.tempMax); Serial.println("°F");
  } else {
    // Fallback to current temp if no daily data
    detailedWeatherData.tempMin = temp;
    detailedWeatherData.tempMax = temp;
  }
  
  // Extract hourly forecast (next 24 hours)
  JsonArray hourly = doc["hourly"];
  detailedWeatherData.hourlyCount = 0;
  for(int i = 0; i < min(24, (int)hourly.size()); i++) {
    JsonObject hour = hourly[i];
    detailedWeatherData.hourly[i].hour = hour["dt"] | 0;
    detailedWeatherData.hourly[i].temp = hour["temp"] | 0.0;
    detailedWeatherData.hourly[i].humidity = hour["humidity"] | 0;
    detailedWeatherData.hourly[i].windSpeed = hour["wind_speed"] | 0.0;
    detailedWeatherData.hourly[i].clouds = hour["clouds"] | 0;
    
    JsonArray hourWeather = hour["weather"];
    if(hourWeather.size() > 0) {
      detailedWeatherData.hourly[i].condition = hourWeather[0]["main"] | "Unknown";
    } else {
      detailedWeatherData.hourly[i].condition = "Unknown";
    }
    detailedWeatherData.hourlyCount++;
  }
  
  // Update detailed weather data
  detailedWeatherData.city = cityName;
  detailedWeatherData.temp = String((int)temp);
  detailedWeatherData.condition = condition;
  detailedWeatherData.description = description;
  detailedWeatherData.humidity = humidity;
  detailedWeatherData.windSpeed = windSpeed;
  detailedWeatherData.windDirection = windDirection;
  detailedWeatherData.pressure = pressure;
  detailedWeatherData.feelsLike = feelsLike;
  detailedWeatherData.visibility = visibility;
  detailedWeatherData.dewPoint = dewPoint;
  detailedWeatherData.clouds = clouds;
  detailedWeatherData.sunrise = sunrise;
  detailedWeatherData.sunset = sunset;
  
  Serial.println("One Call API weather fetch successful");
  return true;
}

// Fallback to basic weather API
bool tryBasicWeatherAPI(WiFiClient& client) {
  String q = cityQuery; 
  q.replace(" ", "%20");
  String host = "api.openweathermap.org";
  String path = "/data/2.5/weather?q=" + q + "&appid=" + apiKey + "&units=" + units + "&lang=" + language;
  
  Serial.print("Fetching basic weather: "); 
  Serial.print(host); 
  Serial.println(path);
  
  if(!client.connect(host.c_str(), 80)) {
    Serial.println("Basic weather connect fail");
    return false;
  }
  
  client.print("GET " + path + " HTTP/1.1\r\n"
               "Host: " + host + "\r\n"
               "User-Agent: Arduino\r\n"
               "Connection: close\r\n\r\n");
  
  // FIXED: Efficient response reading
  unsigned long startTime = millis();
  
  // Skip headers efficiently
  while(client.connected() && (millis() - startTime) < WEATHER_TIMEOUT) {
    if(client.available()) {
      String line = client.readStringUntil('\n');
      if(line.length() <= 1) { // Empty line = headers done
        Serial.println("Basic API headers complete, reading JSON...");
        break;
      }
    } else {
      delay(1);
    }
  }
  
  // Read JSON response with pre-allocated buffer
  char responseBuffer[8192];
  int totalBytes = 0;
  
  // Wait a moment for data to be available
  delay(50);
  
  // Read all available data with timeout
  unsigned long readStartTime3 = millis();
  while(client.connected() && (millis() - readStartTime3) < 5000) { // 5 second timeout
    if(client.available()) {
      int bytesToRead = min(client.available(), (int)(sizeof(responseBuffer) - totalBytes - 1));
      if(bytesToRead > 0) {
        int bytesRead = client.readBytes(responseBuffer + totalBytes, bytesToRead);
        totalBytes += bytesRead;
        readStartTime3 = millis(); // Reset timeout on successful read
      } else {
        delay(1);
      }
    } else {
      delay(10);
      // If no data for 100ms, assume we're done
      if(millis() - readStartTime3 > 100) {
        break;
      }
    }
  }
  
  responseBuffer[totalBytes] = '\0';
  client.stop();
  
  if(totalBytes == 0) {
    Serial.println("No basic API response data received");
    return false;
  }
  
  Serial.print("Basic API response received: "); 
  Serial.print(totalBytes); 
  Serial.println(" bytes");
  
  // Debug: Print first 200 characters of response
  Serial.print("Basic API response preview: ");
  String response(responseBuffer);
  if(response.length() > 200) {
    Serial.println(response.substring(0, 200));
  } else {
    Serial.println(response);
  }
  
  // Debug: Check if sunrise/sunset data exists
  if(response.indexOf("\"sunrise\"") >= 0) {
    Serial.println("Sunrise/sunset data found in response");
  } else {
    Serial.println("No sunrise/sunset data in Basic Weather API response");
  }
  
  // Parse JSON (single string creation)
  DynamicJsonDocument doc(768);
  DeserializationError err = deserializeJson(doc, response);
  if(err) {
    Serial.print("Basic weather parse error: "); 
    Serial.println(err.f_str());
    return false;
  }
  
  // Extract weather data
  float temp = doc["main"]["temp"] | 0.0;
  float tempMin = doc["main"]["temp_min"] | temp; // Daily low from basic API
  float tempMax = doc["main"]["temp_max"] | temp; // Daily high from basic API
  int humidity = doc["main"]["humidity"] | 0;
  int pressure = doc["main"]["pressure"] | 0;
  float feelsLike = doc["main"]["feels_like"] | 0.0;
  String condition = doc["weather"][0]["main"] | "Unknown";
  String description = doc["weather"][0]["description"] | "";
  String city = doc["name"] | "???";
  
  // Extract wind data
  float windSpeed = doc["wind"]["speed"] | 0.0;
  int windDeg = doc["wind"]["deg"] | 0;
  String windDirection = getWindDirection(windDeg);
  
  // Extract visibility
  int visibility = doc["visibility"] | 0;
  
  // Extract clouds
  int clouds = doc["clouds"]["all"] | 0;
  
  // Extract sunrise/sunset (convert from UTC to local time)
  int sunrise = doc["sys"]["sunrise"] | 0;
  int sunset = doc["sys"]["sunset"] | 0;
  
  Serial.print("Raw sunrise: "); Serial.println(sunrise);
  Serial.print("Raw sunset: "); Serial.println(sunset);
  Serial.print("Timezone offset: "); Serial.println(localTimeOffsetInSecs);
  
  // Convert from UTC to local time if we have timezone offset
  if(sunrise > 0 && sunset > 0) {
    sunrise += localTimeOffsetInSecs; // Add timezone offset in seconds
    sunset += localTimeOffsetInSecs;   // Add timezone offset in seconds
    Serial.print("Adjusted sunrise: "); Serial.println(sunrise);
    Serial.print("Adjusted sunset: "); Serial.println(sunset);
  } else {
    // If no sunrise/sunset data, set to 0 to indicate not available
    sunrise = 0;
    sunset = 0;
    Serial.println("No sunrise/sunset data available");
  }
  
  // Update detailed weather data (basic info only)
  detailedWeatherData.city = city;
  detailedWeatherData.temp = String((int)temp);
  detailedWeatherData.tempMin = tempMin; // Daily low
  detailedWeatherData.tempMax = tempMax; // Daily high
  detailedWeatherData.condition = condition;
  detailedWeatherData.description = description;
  detailedWeatherData.humidity = humidity;
  detailedWeatherData.windSpeed = windSpeed;
  detailedWeatherData.windDirection = windDirection;
  detailedWeatherData.pressure = pressure;
  detailedWeatherData.feelsLike = feelsLike;
  detailedWeatherData.visibility = visibility;
  detailedWeatherData.dewPoint = 0.0; // Not available in basic API
  detailedWeatherData.clouds = clouds;
  detailedWeatherData.sunrise = sunrise;
  detailedWeatherData.sunset = sunset;
  
  Serial.print("Basic API - Daily Low: "); Serial.print(tempMin);
  Serial.print("°F, High: "); Serial.print(tempMax); Serial.println("°F");
  
  // No hourly data in basic API
  detailedWeatherData.hourlyCount = 0;
  
  Serial.println("Basic weather fetch successful");
  return true;
}



// --- Request Weather Update ---
void requestWeatherUpdate() {
  if(detailedWeatherData.state == WEATHER_IDLE) {
    weatherRequested = true;
    Serial.println("Weather update requested");
  } else if(detailedWeatherData.state == WEATHER_FAILED) {
    // Reset failed state to allow retry
    detailedWeatherData.state = WEATHER_IDLE;
    weatherRequested = true;
    Serial.println("Weather retry requested");
  } else {
    Serial.print("Weather update skipped - current state: ");
    Serial.println(detailedWeatherData.state);
  }
}

// --- Request NTP Update ---
void requestNTPUpdate() {
  ntpRequested = true;
}

// --- Non-blocking NTP Update ---
bool updateLocalTimeNonBlocking() {
  // Try multiple NTP servers with enhanced error handling
  for(int attempt = 0; attempt < numNtpServers; attempt++) {
    // Update NTP server
    timeClient.setPoolServerName(ntpServers[currentNtpServer]);
    Serial.print("Trying NTP server: ");
    Serial.println(ntpServers[currentNtpServer]);
    
    // Try to update with longer timeout and better error handling
    timeClient.setUpdateInterval(45000); // 45 second timeout
    
    // Force update with timeout monitoring
    unsigned long startTime = millis();
    bool updateSuccess = timeClient.forceUpdate();
    
    if(updateSuccess && (millis() - startTime) < 30000) { // Success within 30 seconds
      Serial.println("NTP update successful");
      return true;
    } else {
      Serial.print("NTP update failed for ");
      Serial.println(ntpServers[currentNtpServer]);
      
      // Try next server
      currentNtpServer = (currentNtpServer + 1) % numNtpServers;
      delay(500); // Brief delay between servers
    }
  }
  
  Serial.println("All NTP servers failed");
  return false;
}

// --- Get Current Weather Data ---
void getWeatherData(String& city, String& temp, String& condition, bool& dirty) {
  city = detailedWeatherData.city;
  temp = detailedWeatherData.temp;
  condition = detailedWeatherData.condition;
  dirty = detailedWeatherData.dirty;
  detailedWeatherData.dirty = false;
}

// --- Get Detailed Weather Data ---
void getDetailedWeatherData(DetailedWeatherData& data) {
  data = detailedWeatherData;
  detailedWeatherData.dirty = false;
}

// --- Weather Thread Callback (Legacy - will be removed) ---
void weatherThreadCallback() {
  // This is now handled by the FreeRTOS task
}

// --- Beep Logic ---
void beepDAC(uint16_t level, unsigned long durationMs, bool force){
  if(!beepEnabled && !force) return;
  analogWrite(DAC_PIN, level);
  delay(durationMs);
  analogWrite(DAC_PIN, 0);
}
void beepOnButtonPress(){ beepDAC(1000, 40, false); }
bool modeButtonPressed(){
  if(digitalRead(MODE_BUTTON_PIN)==LOW){
    // Normal button press
    if(millis()-lastModeButtonPress>debounceDelay){
      lastModeButtonPress=millis(); 
      beepOnButtonPress(); 
      return true;
    }
  }
  return false;
}
bool adjustButtonPressed(){
  if(digitalRead(ADJUST_BUTTON_PIN)==LOW){
    if(!adjustButtonIsPressed){
      adjustButtonIsPressed=true;
      adjustPressStartTime=millis();
      beepOnButtonPress();
      return true;
    } else {
      // Check for long press (5 seconds) to toggle display
      if(millis()-adjustPressStartTime>displayOffDelay && currentMode == SHOW_MAIN){
        displayOn = !displayOn;
        if(displayOn) {
          display.ssd1306_command(SSD1306_DISPLAYON);
          Serial.println("Display turned ON");
          displayJustTurnedOff = false; // Reset flag when turning on
        } else {
          display.ssd1306_command(SSD1306_DISPLAYOFF);
          Serial.println("Display turned OFF");
          displayJustTurnedOff = true; // Set flag when turning off
          displayTurnOffTime = millis();
        }
        adjustButtonIsPressed = false;
        return false; // Don't trigger normal adjust action
      }
      
      // Normal auto-increment logic
      if(!autoIncrementBy5){
        if(millis()-adjustPressStartTime>adjust5Delay){
          autoIncrementBy5=true;
          lastAutoIncrementTime=millis();
        }
      } else {
        if(millis()-lastAutoIncrementTime>250){
          lastAutoIncrementTime=millis();
          return true;
        }
      }
    }
  } else { 
    adjustButtonIsPressed=false; 
    autoIncrementBy5=false; 
  }
  return false;
}

// Startup progress bar function
void drawProgressBar(int progress, int total, const String& message) {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(message);
  
  // Draw progress bar
  int barWidth = 120;
  int barHeight = 8;
  int barX = 4;
  int barY = 20;
  
  // Background
  display.drawRect(barX, barY, barWidth, barHeight, WHITE);
  
  // Progress
  int fillWidth = (progress * barWidth) / total;
  if(fillWidth > 0) {
    display.fillRect(barX + 1, barY + 1, fillWidth - 1, barHeight - 2, WHITE);
  }
  
  // Percentage
  display.setCursor(0, 30);
  display.print(progress);
  display.print("/");
  display.print(total);
  display.print(" (");
  display.print((progress * 100) / total);
  display.print("%)");
  
  display.display();
}

// --- Setup ---
void setup(){
#if HAS_MATRIX
  matrix.begin();
#endif
  Serial.begin(9600);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(ADJUST_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DAC_PIN, OUTPUT); analogWrite(DAC_PIN, 0);

  // Initialize display first
  if(!display.begin(SSD1306_SWITCHCAPVCC,0x3C)){
    Serial.println("SSD1306 init fail"); for(;;);
  }
  display.clearDisplay(); 
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1); 
  display.setCursor(0,0); 
  display.println("Alarm Clock Starting...");
  display.display(); 
  delay(500);

  // Check WiFi credentials
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Checking WiFi...");
  display.display();
  
  if (EEPROM.read(EEPROM_VALID_ADDR) != EEPROM_VALID_VALUE) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("No WiFi credentials");
    display.setCursor(0,10);
    display.println("Please program via");
    display.setCursor(0,20);
    display.println("serial console");
    display.display();
    Serial.println(F("No valid WiFi credentials. Please program via serial."));
    promptWiFiCredentials();
  }

  // Load WiFi credentials
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Loading settings...");
  display.display();
  
  String ssid = readStringFromEEPROM(EEPROM_SSID_ADDR, 32);
  String pwd  = readDecryptedPasswordFromEEPROM(EEPROM_PWD_ADDR, 32);
  ssid.toCharArray(wifiSSID, 33);
  pwd.toCharArray(wifiPWD, 33);

  loadAlarmFromEEPROM();

  // Connect to WiFi with static IP first, then DHCP fallback
  Serial.print("Connecting Wifi: "); Serial.println(wifiSSID);
  
  // Try static IP first (if enabled), then DHCP fallback
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Connecting WiFi...");
  display.setCursor(0,10);
  display.println("Trying static IP...");
  display.display();
  
  bool connected = false;
  
  if(useStaticIP) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Connecting WiFi...");
    display.setCursor(0,10);
    display.println("Trying static IP...");
    display.display();
    
    // Configure WiFi with static IP and DNS
    WiFi.config(staticIP, dns, gateway, subnet);
    WiFi.begin(wifiSSID, wifiPWD);
    
    // Try static IP for 10 seconds
    for(int i=0; i<20 && WiFi.status()!=WL_CONNECTED; i++){ 
      drawProgressBar(i+1, 20, "Connecting WiFi (Static IP)...");
      delay(500); 
      Serial.print("."); 
    }
    
          if(WiFi.status() == WL_CONNECTED) {
        connected = true;
        Serial.println("\nWiFi connected with static IP");
        Serial.print("Static IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("Gateway: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("DNS: ");
        Serial.println(WiFi.dnsIP());
      } else {
        // Try DHCP fallback
        display.clearDisplay();
        display.setCursor(0,0);
        display.println("Static IP failed");
        display.setCursor(0,10);
        display.println("Trying DHCP...");
        display.display();
        delay(1000);
        
        WiFi.disconnect();
        delay(1000);
        WiFi.begin(wifiSSID, wifiPWD);
        
        for(int i=0; i<20 && WiFi.status()!=WL_CONNECTED; i++){ 
          drawProgressBar(i+1, 20, "Connecting WiFi (DHCP)...");
          delay(500); 
          Serial.print("."); 
        }
        
        if(WiFi.status() == WL_CONNECTED) {
          connected = true;
          Serial.println("\nWiFi connected with DHCP");
          Serial.print("DHCP IP: ");
          Serial.println(WiFi.localIP());
          Serial.print("Gateway: ");
          Serial.println(WiFi.gatewayIP());
          Serial.print("DNS: ");
          Serial.println(WiFi.dnsIP());
        }
      }
  } else {
    // Use DHCP only
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Connecting WiFi...");
    display.setCursor(0,10);
    display.println("Using DHCP...");
    display.display();
    
    WiFi.begin(wifiSSID, wifiPWD);
    
    for(int i=0; i<20 && WiFi.status()!=WL_CONNECTED; i++){ 
      drawProgressBar(i+1, 20, "Connecting WiFi (DHCP)...");
      delay(500); 
      Serial.print("."); 
    }
    
    if(WiFi.status() == WL_CONNECTED) {
      connected = true;
      Serial.println("\nWiFi connected with DHCP");
    }
  }
  
  if(WiFi.status()==WL_CONNECTED){
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("WiFi Connected!");
    display.setCursor(0,10);
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.setCursor(0,20);
    if(useStaticIP && WiFi.localIP() == staticIP) {
      display.println("Static IP");
    } else {
      display.println("DHCP");
    }
    display.display();
    delay(1000);
    
    Serial.println("\nWiFi connected");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    if(useStaticIP && WiFi.localIP() == staticIP) {
      Serial.println("Using static IP");
    } else {
      Serial.println("Using DHCP");
    }

    // Start web server
    server.begin();
    Serial.println("Web server started");

    // Check location data
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Checking location...");
    display.display();
    
    String savedSSID = readStringFromEEPROM(EEPROM_SSID_ADDR, 32);
    String city, country, timezone;
    loadLocationFromEEPROM(city, country, timezone);

    if (shouldCallIP2Location(String(wifiSSID))) {
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Getting location...");
      display.display();
      fetchIP2LocationAndSetOffset();
    } else {
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Using saved location");
      display.setCursor(0,10);
      display.print("City: ");
      display.println(city);
      display.display();
      delay(500);
      
      cityQuery = city + "," + country;
      parseTimezoneAndSetOffset(timezone);
      Serial.print("Loaded cityQuery: "); Serial.println(cityQuery);
      Serial.print("Loaded timezone: "); Serial.println(timezone);
      initNTP();
    }

    // Sync time
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Syncing time...");
    display.display();
    
    // Wait a moment for network to stabilize
    delay(1000);
    
    // Check DNS resolution first
if(!checkDNSResolution()) {
  Serial.println("DNS resolution failed, trying last known time");
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("DNS failed");
  display.setCursor(0,10);
  display.println("Trying saved time...");
  display.display();
  delay(1000);
  
  loadLastKnownTime(); // Try to load last known time
  if(!usingFallbackTime) {
    // If no saved time, use current millis
    fallbackStartTime = millis();
    usingFallbackTime = true;
  }
  timeHasBeenSet = false;
  lastNTPRetry = millis();
} else {
  // Request initial NTP update (non-blocking)
  // Fetch weather first (prioritize user-visible data)
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Fetching weather...");
  display.display();
  
  // Perform startup weather fetch
  detailedWeatherData.state = WEATHER_FETCHING;
  detailedWeatherData.lastAttempt = millis();
  
  // Try to fetch weather with timeout
  unsigned long startTime = millis();
  bool weatherSuccess = false;
  
  while (millis() - startTime < 8000) { // 8 second timeout
    if (fetchWeatherNonBlocking()) {
      weatherSuccess = true;
      detailedWeatherData.state = WEATHER_SUCCESS;
      detailedWeatherData.lastUpdate = millis();
      detailedWeatherData.dirty = true;
      Serial.println("Startup weather fetch successful");
      break;
    }
    delay(100); // Small delay between attempts
  }
  
  if (!weatherSuccess) {
    detailedWeatherData.state = WEATHER_FAILED;
    Serial.println("Startup weather fetch failed");
  }
  
  // Now do NTP sync (background task)
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Syncing time...");
  display.display();
  
  requestNTPUpdate(); // Initial NTP request
  timeHasBeenSet = false;
  lastNTPRetry = millis();
  
  lastWeatherUpdate = millis();
  lastNTPUpdate = millis();
  }
  } else {
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("WiFi failed");
    display.setCursor(0,10);
    display.println("Running offline");
    display.display();
    delay(1000);
    
    Serial.println("\nWifi fail => offline");
  }

  // Initialize non-blocking state management
  weatherRequested = false;
  ntpRequested = false;
  lastWeatherProcess = 0;
  lastNTPProcess = 0;
  
  Serial.println("Non-blocking weather and NTP system initialized");
}

// --- Main Loop ---
void loop(){
  // --- Serial Console Commands ---
if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("reset")) {
      resetEEPROM();
    } else if (cmd.equalsIgnoreCase("wifi")) {
      promptWiFiCredentials();
    } else if (cmd.equalsIgnoreCase("refresh")) {
      requestWeatherUpdate();
      Serial.println(F("Weather refresh requested!"));
    } else if (cmd.equalsIgnoreCase("static")) {
      useStaticIP = !useStaticIP;
      Serial.print(F("Static IP: "));
      Serial.println(useStaticIP ? "ENABLED" : "DISABLED");
    } else if (cmd.equalsIgnoreCase("ip")) {
      Serial.print(F("Current IP: "));
      Serial.println(WiFi.localIP());
      Serial.print(F("Static IP enabled: "));
      Serial.println(useStaticIP ? "YES" : "NO");
    } else if (cmd.equalsIgnoreCase("sync")) {
      // Force time sync
      Serial.println(F("Forcing time sync..."));
      currentNtpServer = 0;
      ntpRetryCount = 0;
      timeHasBeenSet = false;
      usingFallbackTime = false;
      requestNTPUpdate();
    } else if (cmd.equalsIgnoreCase("diagnostics")) {
      performNetworkDiagnostics();
    } else if (cmd.equalsIgnoreCase("resetlocation")) {
      // Reset timezone and location, then perform IP2Location request
      Serial.println(F("Resetting location and timezone..."));
      
      // Clear saved location data from EEPROM
      for (int i = EEPROM_CITY_ADDR; i < EEPROM_CITY_ADDR + 32; ++i) EEPROM.update(i, 0);
      for (int i = EEPROM_COUNTRY_ADDR; i < EEPROM_COUNTRY_ADDR + 8; ++i) EEPROM.update(i, 0);
      for (int i = EEPROM_TZ_ADDR; i < EEPROM_TZ_ADDR + 8; ++i) EEPROM.update(i, 0);
      
      // Reset timezone offset
      localTimeOffsetInSecs = 0;
      timeClient.setTimeOffset(0);
      
      // Reset time sync status
      timeHasBeenSet = false;
      usingFallbackTime = false;
      ntpRetryCount = 0;
      currentNtpServer = 0;
      
      // Reset weather data
      detailedWeatherData.state = WEATHER_IDLE;
      detailedWeatherData.dirty = true;
      
      Serial.println(F("Location data cleared from EEPROM"));
      Serial.println(F("Timezone offset reset to UTC"));
      Serial.println(F("Time sync reset"));
      Serial.println(F("Weather data reset"));
      
      // Perform IP2Location request if WiFi is connected
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("Performing IP2Location request..."));
        fetchIP2LocationAndSetOffset();
        
        // Request weather update with new location
        Serial.println(F("Requesting weather update with new location..."));
        requestWeatherUpdate();
      } else {
        Serial.println(F("WiFi not connected - IP2Location skipped"));
      }
      
      Serial.println(F("Location reset complete!"));
    } else if (cmd.startsWith("settime ")) {
      // Manual time set: "settime HH:MM"
      String timeStr = cmd.substring(8);
      int colonPos = timeStr.indexOf(':');
      if(colonPos > 0) {
        int hour = timeStr.substring(0, colonPos).toInt();
        int minute = timeStr.substring(colonPos + 1).toInt();
        if(hour >= 0 && hour < 24 && minute >= 0 && minute < 60) {
          // Set fallback time manually
          fallbackStartTime = millis() - ((hour * 3600 + minute * 60) * 1000);
          usingFallbackTime = true;
          timeHasBeenSet = false;
          Serial.print(F("Manual time set to: "));
          Serial.print(hour);
          Serial.print(":");
          Serial.println(minute);
        }
      }
    }
}

  handleButtons();
  if (currentMode == SHOW_MAIN) checkAlarm();

  // Non-blocking alarm beep logic
  if (alarmSounding) {
    unsigned long now = millis();
    if (now - alarmStartTime > alarmDuration) {
      stopAlarm();
    } else {
      if (!beepActive && now - lastBeepTime >= beepInterval) {
        beepDAC(2000, beepLength, true);
        beepActive = true;
        lastBeepTime = now;
      }
      if (beepActive && now - lastBeepTime >= beepLength) {
        analogWrite(DAC_PIN, 0);
        beepActive = false;
      }
    }
  }

  // NTP update every 10 seconds for accurate seconds sync (but not during alarm)
  if (WiFi.status() == WL_CONNECTED && !alarmSounding && millis() - lastNTPUpdate > ntpUpdateInterval) {
    requestNTPUpdate();
    lastNTPUpdate = millis();
  }
  
  // Enhanced NTP retry logic with network diagnostics (but not during alarm)
  if (WiFi.status() == WL_CONNECTED && !alarmSounding && !timeHasBeenSet && ntpRetryCount < maxNTPRetries && 
      millis() - lastNTPRetry > ntpRetryInterval) {
    Serial.print("NTP retry attempt "); Serial.print(ntpRetryCount + 1); Serial.print("/"); Serial.println(maxNTPRetries);
    
    // Perform network diagnostics on first few retries
    if(ntpRetryCount < 3) {
      performNetworkDiagnostics();
    }
    
    // Reset to first NTP server for retry
    currentNtpServer = 0;
    requestNTPUpdate();
    lastNTPRetry = millis();
  }
  
  // Periodic network health check (but not during alarm)
  if (WiFi.status() == WL_CONNECTED && !alarmSounding && millis() - lastNetworkCheck > networkCheckInterval) {
    performNetworkDiagnostics();
    lastNetworkCheck = millis();
  }

  // Weather update timer (non-blocking) - but not during alarm
  if (WiFi.status() == WL_CONNECTED && !alarmSounding && millis() - lastWeatherUpdate > weatherInterval) {
    requestWeatherUpdate();
    lastWeatherUpdate = millis();
  }
  
  // Process weather and NTP states (non-blocking)
  if (millis() - lastWeatherProcess > PROCESS_INTERVAL) {
    processWeatherState();
    lastWeatherProcess = millis();
  }
  
  if (millis() - lastNTPProcess > PROCESS_INTERVAL) {
    processNTPState();
    lastNTPProcess = millis();
  }
  
  // Handle weather retries if needed
  if (WiFi.status() == WL_CONNECTED && !alarmSounding && detailedWeatherData.state == WEATHER_FAILED && 
      detailedWeatherData.retryCount < MAX_WEATHER_RETRIES && 
      millis() - detailedWeatherData.lastAttempt > WEATHER_RETRY_INTERVAL) {
    requestWeatherUpdate();
  }

  // Screen inversion during alarm
  if (alarmSounding) {
    if (millis() - lastInvertToggle > invertInterval) {
      invertState = !invertState;
      lastInvertToggle = millis();
    }
    display.invertDisplay(invertState);
  } else {
    if (invertState) {
      display.invertDisplay(false);
      invertState = false;
    }
  }

#if HAS_MATRIX
  static bool matrixState = false;
  if (alarmSounding && ledMatrixEnabled) {
    if (invertState != matrixState) {
      matrixState = invertState;
      if (matrixState) {
        matrix.renderBitmap(allOnBitmap, 12, 8); // All 12x8 LEDs on
      } else {
        matrix.clear();
      }
    }
  } else {
    matrix.clear();
    matrixState = false;
  }
#endif

  // Reset display just turned off flag after 2 seconds
  if(displayJustTurnedOff && millis() - displayTurnOffTime > 2000) {
    displayJustTurnedOff = false;
  }

  // Process web requests (simple approach)
  processWebRequests();
  
  updateDisplay();
  delay(50);
}
// --- Alarm/Clock/Weather Logic ---
void handleButtons(){
  if(alarmSounding){
    if(adjustButtonPressed()||modeButtonPressed()) stopAlarm();
    return;
  }
  
  // Handle mode button first
  if(modeButtonPressed()){
    // Turn on display if it was off (but not if just turned off)
    if(!displayOn && !displayJustTurnedOff) {
      displayOn = true;
      display.ssd1306_command(SSD1306_DISPLAYON);
      delay(50); // Small delay to prevent flicker
      Serial.println("Display turned ON by mode button");
    }
    
    // Continue with normal mode button logic
    switch(currentMode){
      case SHOW_MAIN:
        currentMode=SET_ALARM_HOUR;
        tempAlarmHour=alarmHour; tempAlarmMinute=alarmMinute;
        break;
      case SET_ALARM_HOUR: currentMode=SET_ALARM_MINUTE; break;
      case SET_ALARM_MINUTE:
        alarmHour=tempAlarmHour; alarmMinute=tempAlarmMinute; alarmEnabled=true;
        saveAlarmToEEPROM(); currentMode=SHOW_ALARM_STATUS; break;
      case SHOW_ALARM_STATUS: currentMode=MENU_BEEP; break;
      case MENU_BEEP: currentMode=MENU_TIMEFORMAT; break;
      case MENU_TIMEFORMAT:
        #if HAS_MATRIX
          currentMode = MENU_LED;
          break;
        #else
          currentMode = SHOW_MAIN;
          break;
        #endif
      #if HAS_MATRIX
      case MENU_LED:
        currentMode = SHOW_MAIN;
        break;
      #endif
      default:
        currentMode = SHOW_MAIN;
        break;
    }
    return; // Exit early to avoid double processing
  }
  
  // Handle adjust button
  if(adjustButtonPressed()){
    // Turn on display if it was off (but not if just turned off)
    if(!displayOn && !displayJustTurnedOff) {
      displayOn = true;
      display.ssd1306_command(SSD1306_DISPLAYON);
      delay(50); // Small delay to prevent flicker
      Serial.println("Display turned ON by adjust button");
    }
    
    // Continue with normal adjust button logic
    switch(currentMode){
      case SHOW_MAIN: break;
      case SET_ALARM_HOUR:
        if(!autoIncrementBy5) tempAlarmHour=(tempAlarmHour+1)%24;
        else                 tempAlarmHour=(tempAlarmHour+5)%24;
        break;
      case SET_ALARM_MINUTE:
        if(!autoIncrementBy5) tempAlarmMinute=(tempAlarmMinute+1)%60;
        else                  tempAlarmMinute=(tempAlarmMinute+5)%60;
        break;
      case SHOW_ALARM_STATUS: alarmEnabled = !alarmEnabled; saveAlarmToEEPROM(); break;
      case MENU_BEEP: beepEnabled = !beepEnabled; saveAlarmToEEPROM(); break;
      case MENU_TIMEFORMAT: use24hr = !use24hr; saveAlarmToEEPROM(); break;
      #if HAS_MATRIX
      case MENU_LED:
        ledMatrixEnabled = !ledMatrixEnabled;
        saveAlarmToEEPROM();
        break;
      #endif
    }
  }
}

void checkAlarm(){
  if(currentMode != SHOW_MAIN) return;
  if(alarmEnabled && !alarmSounding && (timeHasBeenSet || usingFallbackTime)){
    int hh, mm;
    
    if(timeHasBeenSet) {
      hh = timeClient.getHours();
      mm = timeClient.getMinutes();
    } else {
      // Use fallback time for alarm
      unsigned long elapsed = (millis() - fallbackStartTime) / 1000;
      hh = (elapsed / 3600) % 24;
      mm = (elapsed / 60) % 60;
    }
    
    if(hh == alarmHour && mm == alarmMinute && !alarmTriggeredThisMinute){
      alarmTriggeredThisMinute = true;
      soundAlarm();
    }
    if(mm != lastCheckedMinute){
      alarmTriggeredThisMinute = false;
      lastCheckedMinute = mm;
    }
  }
}

void soundAlarm(){
  Serial.println("ALARM!");
  alarmSounding = true;
  alarmStartTime = millis();
  lastBeepTime = 0;
  beepActive = false;
  
  // Cancel any ongoing network requests
  // Weather and NTP tasks will continue but won't affect alarm
  
  // Turn on display when alarm goes off
  if(!displayOn) {
    displayOn = true;
    display.ssd1306_command(SSD1306_DISPLAYON);
    delay(50); // Small delay to prevent flicker
    Serial.println("Display turned ON by alarm");
  }
}

void waitForButtonRelease() {
  while (digitalRead(MODE_BUTTON_PIN) == LOW || digitalRead(ADJUST_BUTTON_PIN) == LOW) {
    delay(10);
  }
}

void stopAlarm(){
  Serial.println("Alarm stopped");
  alarmSounding = false;
  analogWrite(DAC_PIN, 0);
  beepActive = false;
  waitForButtonRelease();
}

void saveAlarmToEEPROM(){
  EEPROM.update(EEPROM_VALID_ADDR,EEPROM_VALID_VALUE);
  EEPROM.update(EEPROM_ALARM_HOUR_ADDR, alarmHour);
  EEPROM.update(EEPROM_ALARM_MINUTE_ADDR, alarmMinute);
  EEPROM.update(EEPROM_ALARM_ENABLED_ADDR, alarmEnabled?1:0);
  EEPROM.update(EEPROM_BEEP_ENABLED_ADDR, beepEnabled?1:0);
  EEPROM.update(EEPROM_TIMEFORMAT_ADDR, use24hr?1:0);
  EEPROM.update(EEPROM_LED_MATRIX_ADDR, ledMatrixEnabled ? 1 : 0);
  Serial.println("Alarm/settings saved");
}
void loadAlarmFromEEPROM(){
  if(EEPROM.read(EEPROM_VALID_ADDR)==EEPROM_VALID_VALUE){
    alarmHour=EEPROM.read(EEPROM_ALARM_HOUR_ADDR);
    alarmMinute=EEPROM.read(EEPROM_ALARM_MINUTE_ADDR);
    alarmEnabled=(EEPROM.read(EEPROM_ALARM_ENABLED_ADDR)==1);
    beepEnabled=(EEPROM.read(EEPROM_BEEP_ENABLED_ADDR)==1);
    use24hr=(EEPROM.read(EEPROM_TIMEFORMAT_ADDR)==1);
    ledMatrixEnabled = (EEPROM.read(EEPROM_LED_MATRIX_ADDR) == 1);
    Serial.println("Alarm/settings loaded");
  } else {
    Serial.println("No valid data => defaults");
  }
  if(alarmHour<0||alarmHour>23) alarmHour=7;
  if(alarmMinute<0||alarmMinute>59) alarmMinute=0;
  tempAlarmHour=alarmHour;
  tempAlarmMinute=alarmMinute;
}

// Save last known good time to EEPROM
void saveLastKnownTime() {
  if(timeHasBeenSet) {
    EEPROM.update(EEPROM_LAST_TIME_HOUR, timeClient.getHours());
    EEPROM.update(EEPROM_LAST_TIME_MINUTE, timeClient.getMinutes());
    EEPROM.update(EEPROM_LAST_TIME_SECOND, timeClient.getSeconds());
    // Note: We don't save date as it's less critical for alarm functionality
    Serial.println("Last known time saved to EEPROM");
  }
}

// Load last known time from EEPROM as fallback
void loadLastKnownTime() {
  int savedHour = EEPROM.read(EEPROM_LAST_TIME_HOUR);
  int savedMinute = EEPROM.read(EEPROM_LAST_TIME_MINUTE);
  
  if(savedHour >= 0 && savedHour < 24 && savedMinute >= 0 && savedMinute < 60) {
    // Calculate how much time has passed since last save
    unsigned long currentMillis = millis();
    unsigned long timeSinceLastSave = currentMillis - (savedHour * 3600 + savedMinute * 60) * 1000;
    
    // Set fallback time based on saved time + elapsed time
    fallbackStartTime = currentMillis - timeSinceLastSave;
    usingFallbackTime = true;
    timeHasBeenSet = false;
    
    Serial.print("Loaded last known time: ");
    Serial.print(savedHour);
    Serial.print(":");
    Serial.println(savedMinute);
  }
}

void updateDisplay(){
  if(!displayOn) {
    // Show a small indicator that the device is still working
    static unsigned long lastBlink = 0;
    static bool showIndicator = false;
    if(millis() - lastBlink > 2000) { // Blink every 2 seconds
      showIndicator = !showIndicator;
      lastBlink = millis();
    }
    if(showIndicator) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("OFF");
      display.display();
    }
    return;
  }
  
  static String lastWeatherCity, lastWeatherTemp, lastWeatherCondition;
  display.clearDisplay();
  switch(currentMode){
    case SHOW_MAIN:
    default: {
      drawTimeTop();
      
      // Get current weather data
      String currentCity, currentTemp, currentCondition;
      bool weatherDirty;
      getWeatherData(currentCity, currentTemp, currentCondition, weatherDirty);
      
      // Show fetching indicator if weather is being fetched
      if(detailedWeatherData.state == WEATHER_FETCHING) {
        display.setTextSize(1);
        display.setCursor(0, 16);
        display.print("Fetching...");
        display.setCursor(0, 24);
        display.print("Weather");
      } else if(weatherDirty) {
        drawPaneWeather(currentTemp, currentCity, currentCondition);
        lastWeatherCity = currentCity;
        lastWeatherTemp = currentTemp;
        lastWeatherCondition = currentCondition;
      } else {
        display.setTextSize(2);
        display.setCursor(0,16);
        display.print(lastWeatherTemp);
        int16_t x = display.getCursorX();
        int16_t y = display.getCursorY();
        display.drawCircle(x + 2, y + 3, 2, SSD1306_WHITE);
        display.setCursor(x + 8, y);
        display.print("F");
        int lineX=44;
        display.drawLine(lineX,16,lineX,31,WHITE);
        display.setTextSize(1);
        int left=lineX+2;
        display.setCursor(left,16);
        String cName = lastWeatherCity.length()>10?lastWeatherCity.substring(0,10)+"..":lastWeatherCity;
        display.print(cName);
        display.setCursor(left,24);
        String cond = lastWeatherCondition.length()>10?lastWeatherCondition.substring(0,10)+"..":lastWeatherCondition;
        display.print(cond);
      }
      break;
    }
    case SET_ALARM_HOUR: {
      display.setTextSize(1); display.setCursor(0,0); display.print("Set Alarm Hr");
      display.setTextSize(2); display.setCursor(0,16); char buf[5];
      zeroPad2(buf,tempAlarmHour); display.print(buf); break;
    }
    case SET_ALARM_MINUTE: {
      display.setTextSize(1); display.setCursor(0,0); display.print("Set Alarm Min");
      display.setTextSize(2); display.setCursor(0,16); char buf[5];
      zeroPad2(buf,tempAlarmMinute); display.print(buf); break;
    }
    case SHOW_ALARM_STATUS: {
      display.setTextSize(2); display.setCursor(0,0); char buf[5];
      zeroPad2(buf,alarmHour); display.print(buf); display.print(":");
      zeroPad2(buf,alarmMinute); display.print(buf);
      display.setCursor(0,16); display.print(alarmEnabled?"Enabled":"Disabled"); break;
    }
    case MENU_BEEP: {
      display.setTextSize(1); display.setCursor(0,0); display.print("Beep on Btn:");
      display.setCursor(0,16); display.setTextSize(2); display.print(beepEnabled?"ON":"OFF"); break;
    }
    case MENU_TIMEFORMAT: {
      display.setTextSize(1); display.setCursor(0,0); display.print("Time Format:");
      display.setCursor(0,16); display.setTextSize(2); display.print(use24hr?"24hr":"12hr"); break;
    }
#if HAS_MATRIX
    case MENU_LED: {
      display.setTextSize(1); display.setCursor(0,0); display.print("LED");
      display.setCursor(0,16); display.setTextSize(2); display.print(ledMatrixEnabled ? "ON" : "OFF");
      break;
    }
#endif
  }
  display.display();
}

void drawTimeTop(){
  display.setTextSize(2);
  display.setCursor(0,0);

  if(WiFi.status()==WL_CONNECTED && timeHasBeenSet){
    int hh = timeClient.getHours();
    int mm = timeClient.getMinutes();
    int ss = timeClient.getSeconds();
    bool showColon = (ss % 2 == 0);

    if(use24hr){
      char buf[6];
      if(showColon)
        sprintf(buf, "%02d:%02d", hh, mm);
      else
        sprintf(buf, "%02d %02d", hh, mm);
      display.print(buf);
    } else {
      int h12 = hh % 12; if(h12 == 0) h12 = 12;
      display.print(h12);
      if(showColon)
        display.print(":");
      else
        display.print(" ");
      if(mm < 10) display.print("0");
      display.print(mm);
      display.print(hh < 12 ? " AM" : " PM");
    }
  } else if(WiFi.status()==WL_CONNECTED && !timeHasBeenSet && usingFallbackTime){
    // Use fallback time when NTP fails
    unsigned long elapsed = (millis() - fallbackStartTime) / 1000; // seconds since start
    int hh = (elapsed / 3600) % 24;
    int mm = (elapsed / 60) % 60;
    int ss = elapsed % 60;
    bool showColon = (ss % 2 == 0);

    if(use24hr){
      char buf[6];
      if(showColon)
        sprintf(buf, "%02d:%02d", hh, mm);
      else
        sprintf(buf, "%02d %02d", hh, mm);
      display.print(buf);
    } else {
      int h12 = hh % 12; if(h12 == 0) h12 = 12;
      display.print(h12);
      if(showColon)
        display.print(":");
      else
        display.print(" ");
      if(mm < 10) display.print("0");
      display.print(mm);
      display.print(hh < 12 ? " AM" : " PM");
    }
  } else if(WiFi.status()==WL_CONNECTED && !timeHasBeenSet){
    // Show retry status when NTP is failing
    static unsigned long lastBlink = 0;
    static bool showRetry = true;
    if(millis() - lastBlink > 1000) {
      showRetry = !showRetry;
      lastBlink = millis();
    }
    if(showRetry) {
      if(usingFallbackTime) {
        display.print("SAVED");
      } else {
        display.print("SYNC");
      }
    } else {
      display.print("     ");
    }
  } else {
    display.print("OFF");
  }
  drawAlarmTopRight();
}

void drawAlarmTopRight(){
  int x = SCREEN_WIDTH - 18;
  int y = 0;
  display.setTextSize(1);
  char buf[3];
  display.setCursor(x, y);
  zeroPad2(buf, alarmHour);
  display.print(buf);
  display.setCursor(x, y + 8);
  zeroPad2(buf, alarmMinute);
  display.print(buf);
  if (alarmEnabled) {
    display.drawLine(SCREEN_WIDTH - 2, 0, SCREEN_WIDTH - 2, 16, WHITE);
  }
}

void drawPaneWeather(const String& temp, const String& city, const String& condition){
  display.setTextSize(2);
  display.setCursor(0,16);
  display.print(temp);
  int16_t x = display.getCursorX();
  int16_t y = display.getCursorY();
  display.drawCircle(x + 2, y + 3, 2, SSD1306_WHITE);
  display.setCursor(x + 8, y);
  display.print("F");
  int lineX=44;
  display.drawLine(lineX,16,lineX,31,WHITE);

  display.setTextSize(1);
  int left=lineX+2;
  display.setCursor(left,16);
  String cName = city.length()>10?city.substring(0,10)+"..":city;
  display.print(cName);
  display.setCursor(left,24);
  String cond = condition.length()>10?condition.substring(0,10)+"..":condition;
  display.print(cond);
}

// --- IP2Location/Timezone/Weather ---
void fetchIP2LocationAndSetOffset(){
  if(WiFi.status()!=WL_CONNECTED){
    Serial.println("No wifi => skip ip2location");
    return;
  }
  WiFiClient client;
  String host="api.ip2location.io";
  
  // Use IP2Location's auto-detect feature by not specifying an IP
  String path=String("/?key=")+IP2LOCATION_KEY;
  path.replace(" ", "%20");
  Serial.print("IP2Location: "); Serial.print(host); Serial.println(path);
  if(!client.connect(host.c_str(),80)){
    Serial.println("IP2Loc connect fail");
    return;
  }
  client.print(String("GET ")+path+" HTTP/1.1\r\n"
               +"Host: "+host+"\r\n"
               +"User-Agent: Arduino\r\n"
               +"Connection: close\r\n\r\n");
  unsigned long st=millis();
  while(!client.available() && client.connected()){
    if(millis()-st>10000){
      Serial.println("ip2loc header timeout");
      client.stop();
      return;
    }
    delay(10);
  }
  while(client.connected()){
    String line = client.readStringUntil('\n');
    if(line.length()<=1) break;
  }
  String body;
  while(client.available()) body += client.readString();
  client.stop();
  Serial.println("ip2loc RAW:\n" + body);

  int jsonStart = body.indexOf('{');
  int jsonEnd   = body.lastIndexOf('}');
  String json;
  if (jsonStart >= 0 && jsonEnd > jsonStart) {
    json = body.substring(jsonStart, jsonEnd + 1);
  } else {
    Serial.println("ip2loc: JSON not found in response!");
    return;
  }
  Serial.println("ip2loc JSON:\n" + json);

  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("ip2loc parse err: ");
    Serial.println(err.f_str());
    return;
  }
  String tzStr = doc["time_zone"] | "";
  if(tzStr==""){
    Serial.println("No time_zone => skip offset");
    // Set default values if IP2Location fails
    cityQuery = "Los Angeles,US";
    saveLocationToEEPROM("Los Angeles", "US", "America/Los_Angeles");
    parseTimezoneAndSetOffset("America/Los_Angeles");
    return;
  }
  parseTimezoneAndSetOffset(tzStr);

  // Set cityQuery to current location
  String city = doc["city_name"] | "";
  String country = doc["country_code"] | "";
  if(city.length()>0 && country.length()>0){
    cityQuery = city+","+country;
    cityQuery.replace(" ", "%20");
    saveLocationToEEPROM(city, country, tzStr);
    Serial.print("cityQuery set to: "); Serial.println(cityQuery);
  } else {
    // Fallback if city/country not found
    cityQuery = "Los Angeles,US";
    saveLocationToEEPROM("Los Angeles", "US", tzStr);
    Serial.println("Using fallback city: Los Angeles,US");
  }
}

void parseTimezoneAndSetOffset(const String& tzString){
  if(tzString.length()<6){
    Serial.println("tz string too short");
    return;
  }
  
  // Try to parse timezone offset format (e.g., "-08:00", "+05:30")
  char signChar;
  int hour, min;
  if (sscanf(tzString.c_str(), "%c%2d:%2d", &signChar, &hour, &min) == 3) {
    int offsetSecs = hour*3600 + min*60;
    if(signChar=='-') offsetSecs = -offsetSecs;
    localTimeOffsetInSecs = offsetSecs;
    Serial.print("Parsed offset from ip2loc (secs): ");
    Serial.println((int)localTimeOffsetInSecs);
    timeClient.setTimeOffset(localTimeOffsetInSecs);
    initNTP();
  } else {
    // Try to parse timezone name format (e.g., "America/Los_Angeles")
    if(tzString.indexOf("America/Los_Angeles") >= 0) {
      localTimeOffsetInSecs = -8 * 3600; // PST
    } else if(tzString.indexOf("America/New_York") >= 0) {
      localTimeOffsetInSecs = -5 * 3600; // EST
    } else if(tzString.indexOf("Europe/London") >= 0) {
      localTimeOffsetInSecs = 0; // GMT
    } else {
      // Default to UTC if unknown
      localTimeOffsetInSecs = 0;
      Serial.println("Unknown timezone, using UTC");
    }
    Serial.print("Using timezone offset (secs): ");
    Serial.println((int)localTimeOffsetInSecs);
    timeClient.setTimeOffset(localTimeOffsetInSecs);
    initNTP();
  }
}

void initNTP(){
  timeClient.setTimeOffset(localTimeOffsetInSecs);
  timeClient.begin();
}

bool checkDNSResolution(){
  const char* testHosts[] = {
    "time.google.com", 
    "8.8.8.8",  // Google DNS
    "1.1.1.1",   // Cloudflare DNS
    "pool.ntp.org",
  };
  
  Serial.println("=== DNS Resolution Test ===");
  Serial.print("Current DNS: ");
  Serial.println(WiFi.dnsIP());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  
  for(int i = 0; i < 4; i++) {
    WiFiClient client;
    Serial.print("Testing ");
    Serial.print(testHosts[i]);
    Serial.print(": ");
    
    if(client.connect(testHosts[i], 80)) {
      client.stop();
      Serial.println("SUCCESS");
      return true;
    } else {
      Serial.println("FAILED");
    }
    delay(100); // Brief delay between tests
  }
  
  Serial.println("All DNS tests failed");
  return false;
}
bool updateLocalTime(){
  // Try multiple NTP servers with enhanced error handling
  for(int attempt = 0; attempt < numNtpServers; attempt++) {
    // Update NTP server
    timeClient.setPoolServerName(ntpServers[currentNtpServer]);
    Serial.print("Trying NTP server: ");
    Serial.println(ntpServers[currentNtpServer]);
    
    // Try to update with longer timeout and better error handling
    timeClient.setUpdateInterval(45000); // 45 second timeout
    
    // Force update with timeout monitoring
    unsigned long startTime = millis();
    bool updateSuccess = timeClient.forceUpdate();
    
    if(updateSuccess && (millis() - startTime) < 30000) { // Success within 30 seconds
      Serial.println("NTP update successful");
      return true;
    } else {
      Serial.print("NTP update failed for ");
      Serial.println(ntpServers[currentNtpServer]);
      
      // Try next server
      currentNtpServer = (currentNtpServer + 1) % numNtpServers;
      delay(500); // Brief delay between servers
    }
  }
  
  Serial.println("All NTP servers failed");
  return false;
}


// URL decode function
String urlDecode(String input) {
  String decoded = "";
  char a, b;
  for (size_t i = 0; i < input.length(); i++) {
    if (input[i] == '%' && i + 2 < input.length()) {
      a = input[i + 1];
      b = input[i + 2];
      if (isxdigit(a) && isxdigit(b)) {
        if (a >= 'a') a -= 'a' - 'A';
        if (a >= 'A') a -= ('A' - 10);
        else a -= '0';
        if (b >= 'a') b -= 'a' - 'A';
        if (b >= 'A') b -= ('A' - 10);
        else b -= '0';
        decoded += (char)(16 * a + b);
        i += 2;
      } else {
        decoded += input[i];
      }
    } else if (input[i] == '+') {
      decoded += ' ';
    } else {
      decoded += input[i];
    }
  }
  return decoded;
}

// --- Web Server Request Processing ---
void processWebRequest(const String& request, const String& postBody, WiFiClient& client) {
  // Debug: print the full request
  Serial.println("=== FULL REQUEST ===");
  Serial.println(request);
  if (postBody.length() > 0) {
    Serial.println("=== POST BODY ===");
    Serial.println(postBody);
  }
  Serial.println("=== END REQUEST ===");
  
  // Process the request
  if (request.indexOf("GET / ") >= 0 || request.indexOf("GET /?") >= 0) {
    // Serve the dashboard page
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println();

    // Get current weather data
    String weatherCity, weatherTemp, weatherCondition;
    bool weatherDirty;
    getWeatherData(weatherCity, weatherTemp, weatherCondition, weatherDirty);
    
    // Get detailed weather data
    DetailedWeatherData detailedData;
    getDetailedWeatherData(detailedData);
    String weatherDescription = detailedData.description;
    int weatherHumidity = detailedData.humidity;
    float weatherWindSpeed = detailedData.windSpeed;
    String weatherWindDirection = detailedData.windDirection;
    int weatherPressure = detailedData.pressure;
    float weatherFeelsLike = detailedData.feelsLike;
    int weatherVisibility = detailedData.visibility;
    float weatherDewPoint = detailedData.dewPoint;
    int weatherClouds = detailedData.clouds;
    int weatherSunrise = detailedData.sunrise;
    int weatherSunset = detailedData.sunset;
    
    // Get current time
    int hh, mm, ss;
    if (timeHasBeenSet) {
      hh = timeClient.getHours();
      mm = timeClient.getMinutes();
      ss = timeClient.getSeconds();
    } else if (usingFallbackTime) {
      unsigned long elapsed = (millis() - fallbackStartTime) / 1000;
      hh = (elapsed / 3600) % 24;
      mm = (elapsed / 60) % 60;
      ss = elapsed % 60;
    } else {
      hh = mm = ss = 0;
    }
    char timeBuf[9];
    sprintf(timeBuf, "%02d:%02d:%02d", hh, mm, ss);

    // HTML Dashboard with dark mode design
    client.print("<!DOCTYPE html>");
    client.print("<html lang='en'>");
    client.print("<head>");
    client.print("<meta charset='UTF-8'>");
    client.print("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
    client.print("<title>Smart Alarm Clock Dashboard</title>");
    client.print("<meta http-equiv='refresh' content='30'>");
    client.print("<link href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/all.min.css' rel='stylesheet'>");
    client.print("<link href='https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;500&display=swap' rel='stylesheet'>");
    client.print("<style>");
    client.print("* { margin: 0; padding: 0; box-sizing: border-box; }");
    client.print("body { font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: linear-gradient(135deg, #0b090a 0%, #161a1d 50%, #660708 100%); color: #f5f3f4; min-height: 100vh; }");
    client.print(".container { max-width: 1200px; margin: 0 auto; padding: 20px; }");
    client.print(".header { text-align: center; margin-bottom: 30px; }");
    client.print(".header h1 { font-size: 2.5rem; margin-bottom: 10px; color: #e5383b; text-shadow: 0 0 20px rgba(229, 56, 59, 0.3); font-weight: 600; }");
    client.print(".header p { font-size: 1.1rem; opacity: 0.8; color: #b1a7a6; }");
    client.print(".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; margin-bottom: 30px; }");
    client.print(".card { background: rgba(22, 26, 29, 0.8); backdrop-filter: blur(20px); border-radius: 15px; padding: 25px; border: 1px solid rgba(229, 56, 59, 0.2); box-shadow: 0 8px 32px rgba(0,0,0,0.4); transition: all 0.3s ease; }");
    client.print(".card:hover { border-color: rgba(229, 56, 59, 0.4); transform: translateY(-2px); }");
    client.print(".card h2 { color: #e5383b; margin-bottom: 15px; font-size: 1.3rem; display: flex; align-items: center; gap: 10px; font-weight: 500; }");
    client.print(".time-display { font-family: 'JetBrains Mono', monospace; font-size: 3rem; font-weight: 600; text-align: center; margin: 20px 0; color: #e5383b; text-shadow: 0 0 20px rgba(229, 56, 59, 0.4); letter-spacing: 2px; }");
    client.print(".weather-card { text-align: center; }");
    client.print(".weather-icon { font-size: 4rem; margin: 15px 0; color: #e5383b; text-shadow: 0 0 20px rgba(229, 56, 59, 0.3); }");
    client.print(".temperature { font-family: 'JetBrains Mono', monospace; font-size: 2.5rem; font-weight: 600; margin: 10px 0; color: #e5383b; text-shadow: 0 0 15px rgba(229, 56, 59, 0.3); }");
    client.print(".weather-details { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-top: 20px; }");
    client.print(".weather-detail { background: rgba(164, 22, 26, 0.2); padding: 15px; border-radius: 10px; text-align: center; border: 1px solid rgba(229, 56, 59, 0.2); }");
    client.print(".weather-detail i { color: #b1a7a6; margin-right: 8px; }");
    client.print(".weather-detail div { color: #f5f3f4; font-weight: 500; }");
    client.print(".status-indicator { display: inline-block; width: 12px; height: 12px; border-radius: 50%; margin-right: 8px; box-shadow: 0 0 10px currentColor; }");
    client.print(".status-online { background: #4CAF50; color: #4CAF50; }");
    client.print(".status-offline { background: #e5383b; color: #e5383b; }");
    client.print(".status-warning { background: #ff9800; color: #ff9800; }");
    client.print(".form-group { margin-bottom: 15px; }");
    client.print(".form-group label { display: block; margin-bottom: 8px; color: #b1a7a6; font-weight: 500; font-size: 0.95rem; }");
    client.print(".form-group input, .form-group select { width: 100%; padding: 12px; border: 1px solid rgba(229, 56, 59, 0.3); border-radius: 8px; background: rgba(22, 26, 29, 0.8); color: #f5f3f4; font-size: 1rem; font-family: 'Inter', sans-serif; transition: all 0.3s ease; }");
    client.print(".form-group input:focus, .form-group select:focus { outline: none; border-color: #e5383b; box-shadow: 0 0 0 3px rgba(229, 56, 59, 0.2); }");
    client.print(".form-group input::placeholder { color: rgba(177, 167, 166, 0.6); }");
    client.print(".btn { background: linear-gradient(135deg, #a4161a 0%, #ba181b 50%, #e5383b 100%); color: white; border: none; padding: 12px 25px; border-radius: 8px; cursor: pointer; font-size: 1rem; font-weight: 500; font-family: 'Inter', sans-serif; transition: all 0.3s ease; box-shadow: 0 4px 15px rgba(229, 56, 59, 0.3); }");
    client.print(".btn:hover { background: linear-gradient(135deg, #ba181b 0%, #e5383b 50%, #e5383b 100%); transform: translateY(-2px); box-shadow: 0 6px 20px rgba(229, 56, 59, 0.4); }");
    client.print(".btn-secondary { background: rgba(22, 26, 29, 0.8); border: 1px solid rgba(229, 56, 59, 0.3); }");
    client.print(".btn-secondary:hover { background: rgba(164, 22, 26, 0.2); border-color: #e5383b; }");
    client.print(".checkbox-group { display: flex; align-items: center; gap: 12px; margin-bottom: 12px; }");
    client.print(".checkbox-group input[type='checkbox'] { width: auto; margin: 0; accent-color: #e5383b; }");
    client.print(".checkbox-group label { color: #f5f3f4; font-weight: 500; }");
    client.print(".nav-links { display: flex; gap: 15px; margin-bottom: 20px; flex-wrap: wrap; }");
    client.print(".nav-links a { color: #b1a7a6; text-decoration: none; padding: 10px 18px; border-radius: 8px; background: rgba(22, 26, 29, 0.8); border: 1px solid rgba(229, 56, 59, 0.2); transition: all 0.3s ease; font-weight: 500; }");
    client.print(".nav-links a:hover { background: rgba(164, 22, 26, 0.2); border-color: #e5383b; color: #e5383b; transform: translateY(-1px); }");
    client.print("@media (max-width: 768px) { .grid { grid-template-columns: 1fr; } .time-display { font-size: 2rem; } .header h1 { font-size: 2rem; } }");
    client.print("</style>");
    client.print("<script>");
    client.print("function updateTime() {");
    client.print("  var now = new Date();");
    client.print("  var timeStr = now.toLocaleTimeString();");
    client.print("  document.getElementById('currentTime').innerHTML = timeStr;");
    client.print("}");
    client.print("setInterval(updateTime, 1000);");
    client.print("</script>");
    client.print("</head>");
    client.print("<body>");
    client.print("<div class='container'>");
    
    // Header
    client.print("<div class='header'>");
    client.print("<h1><i class='fas fa-moon'></i> Smart Alarm Clock</h1>");
    client.print("<p>Dark mode monitoring and control dashboard</p>");
    client.print("</div>");
    
    // Navigation
    client.print("<div class='nav-links'>");
    client.print("<a href='/'>Dashboard</a>");
    client.print("<a href='/refresh'>Refresh Weather</a>");
    client.print("<a href='/test'>Test Page</a>");
    client.print("</div>");
    
    // Main Grid
    client.print("<div class='grid'>");
    
    // Time Card
    client.print("<div class='card'>");
    client.print("<h2><i class='fas fa-moon'></i> Current Time</h2>");
    client.print("<div class='time-display' id='currentTime'>" + String(timeBuf) + "</div>");
    client.print("<div style='text-align: center; margin-top: 15px;'>");
    client.print("<span class='status-indicator " + String(timeHasBeenSet ? "status-online" : (usingFallbackTime ? "status-warning" : "status-offline")) + "'></span>");
    client.print(String(timeHasBeenSet ? "NTP Synchronized" : (usingFallbackTime ? "Using Fallback Time" : "No Time Sync")));
    client.print("</div>");
    
    // Sunrise and Sunset information
    if(weatherSunrise > 0 && weatherSunset > 0) {
      // Convert timestamps to local time
      time_t sunriseTime = weatherSunrise;
      time_t sunsetTime = weatherSunset;
      struct tm* sunriseInfo = localtime(&sunriseTime);
      struct tm* sunsetInfo = localtime(&sunsetTime);
      
      char sunriseStr[6], sunsetStr[6];
      sprintf(sunriseStr, "%02d:%02d", sunriseInfo->tm_hour, sunriseInfo->tm_min);
      sprintf(sunsetStr, "%02d:%02d", sunsetInfo->tm_hour, sunsetInfo->tm_min);
      
      client.print("<div style='margin-top: 20px; display: grid; grid-template-columns: 1fr 1fr; gap: 15px;'>");
      client.print("<div class='weather-detail'>");
      client.print("<i class='fas fa-sunrise' style='color: #ff9800;'></i>");
      client.print("<div style='color: #ff9800; font-weight: 600;'>Sunrise</div>");
      client.print("<div style='font-family: \"JetBrains Mono\", monospace;'>" + String(sunriseStr) + "</div>");
      client.print("</div>");
      client.print("<div class='weather-detail'>");
      client.print("<i class='fas fa-sunset' style='color: #e91e63;'></i>");
      client.print("<div style='color: #e91e63; font-weight: 600;'>Sunset</div>");
      client.print("<div style='font-family: \"JetBrains Mono\", monospace;'>" + String(sunsetStr) + "</div>");
      client.print("</div>");
      client.print("</div>");
    }
    
    client.print("</div>");
    
    // Weather Card
    client.print("<div class='card weather-card'>");
    client.print("<h2><i class='fas fa-cloud-moon'></i> Weather</h2>");
    
    // Determine if it's night time (between 6 PM and 6 AM)
    bool isNightTime = (hh >= 18 || hh < 6);
    
    // Weather Icon based on condition and time of day
    String weatherIcon = "fa-cloud";
    if (weatherCondition.indexOf("Clear") >= 0) {
        weatherIcon = isNightTime ? "fa-moon" : "fa-sun";
    } else if (weatherCondition.indexOf("Cloud") >= 0) {
        weatherIcon = isNightTime ? "fa-cloud-moon" : "fa-cloud-sun";
    } else if (weatherCondition.indexOf("Rain") >= 0) {
        weatherIcon = isNightTime ? "fa-cloud-moon-rain" : "fa-cloud-rain";
    } else if (weatherCondition.indexOf("Snow") >= 0) {
        weatherIcon = "fa-snowflake";
    } else if (weatherCondition.indexOf("Thunder") >= 0) {
        weatherIcon = isNightTime ? "fa-bolt" : "fa-bolt";
    } else if (weatherCondition.indexOf("Fog") >= 0 || weatherCondition.indexOf("Mist") >= 0) {
        weatherIcon = isNightTime ? "fa-smog" : "fa-smog";
    }
    
    client.print("<div class='weather-icon'><i class='fas " + weatherIcon + "'></i></div>");
    client.print("<div class='temperature'>" + weatherTemp + "°F</div>");
    client.print("<div style='text-align: center; margin: 10px 0; font-size: 0.9rem; color: #b1a7a6;'>" + weatherDescription + "</div>");
    
    // Detailed weather information
    client.print("<div class='weather-details'>");
    client.print("<div class='weather-detail'>");
    client.print("<i class='fas fa-map-marker-alt'></i>");
    client.print("<div>" + weatherCity + "</div>");
    client.print("</div>");
    client.print("<div class='weather-detail'>");
    client.print("<i class='fas fa-thermometer-half'></i>");
    client.print("<div>Feels " + String((int)weatherFeelsLike) + "°F</div>");
    client.print("</div>");
    client.print("</div>");
    
    // Additional weather details
    client.print("<div class='weather-details'>");
    client.print("<div class='weather-detail'>");
    client.print("<i class='fas fa-tint'></i>");
    client.print("<div>" + String(weatherHumidity) + "%</div>");
    client.print("</div>");
    client.print("<div class='weather-detail'>");
    client.print("<i class='fas fa-wind'></i>");
    client.print("<div>" + String(weatherWindSpeed, 1) + " " + weatherWindDirection + "</div>");
    client.print("</div>");
    client.print("</div>");
    
    client.print("<div class='weather-details'>");
    client.print("<div class='weather-detail'>");
    client.print("<i class='fas fa-compress-alt'></i>");
    client.print("<div>" + String(weatherPressure) + " hPa</div>");
    client.print("</div>");
    client.print("<div class='weather-detail'>");
    client.print("<i class='fas fa-eye'></i>");
    client.print("<div>" + String(weatherVisibility/1000) + " km</div>");
    client.print("</div>");
    client.print("</div>");
    
                    // Current temperature range (from Basic Weather API)
                client.print("<div class='weather-details'>");
                client.print("<div class='weather-detail'>");
                client.print("<i class='fas fa-thermometer-empty' style='color: #4CAF50;'></i>");
                client.print("<div style='color: #4CAF50; font-weight: 600;'>Current Low</div>");
                client.print("<div style='font-family: \"JetBrains Mono\", monospace;'>" + String((int)detailedData.tempMin) + "°F</div>");
                client.print("</div>");
                client.print("<div class='weather-detail'>");
                client.print("<i class='fas fa-thermometer-full' style='color: #e5383b;'></i>");
                client.print("<div style='color: #e5383b; font-weight: 600;'>Current High</div>");
                client.print("<div style='font-family: \"JetBrains Mono\", monospace;'>" + String((int)detailedData.tempMax) + "°F</div>");
                client.print("</div>");
                client.print("</div>");
    
    client.print("<div style='margin-top: 15px; font-size: 0.9rem; opacity: 0.8; color: #b1a7a6;'>");
    client.print("<span class='status-indicator " + String(detailedData.state == WEATHER_SUCCESS ? "status-online" : (detailedData.state == WEATHER_FETCHING ? "status-warning" : "status-offline")) + "'></span>");
    client.print(String(detailedData.state == WEATHER_IDLE ? "Idle" : 
                       detailedData.state == WEATHER_FETCHING ? "Fetching..." :
                       detailedData.state == WEATHER_SUCCESS ? "Updated " + String((millis() - lastWeatherUpdate) / 1000) + "s ago" : "Failed"));
    client.print("</div>");
    client.print("</div>");
    
    // Hourly Forecast Card
    client.print("<div class='card'>");
    client.print("<h2><i class='fas fa-clock'></i> Hourly Forecast</h2>");
    client.print("<div style='max-height: 200px; overflow-y: auto;'>");
    
    if(detailedData.hourlyCount > 0) {
      for(int i = 0; i < min(8, detailedData.hourlyCount); i++) { // Show next 8 hours
        int hour = detailedData.hourly[i].hour;
        float temp = detailedData.hourly[i].temp;
        String condition = detailedData.hourly[i].condition;
        int humidity = detailedData.hourly[i].humidity;
        float windSpeed = detailedData.hourly[i].windSpeed;
        
        // Convert timestamp to local time
        time_t timestamp = hour;
        struct tm* timeinfo = localtime(&timestamp);
        char timeStr[6];
        sprintf(timeStr, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
        
        client.print("<div style='display: flex; justify-content: space-between; align-items: center; padding: 8px 0; border-bottom: 1px solid rgba(229, 56, 59, 0.2);'>");
        client.print("<div style='display: flex; align-items: center; gap: 10px;'>");
        client.print("<div style='font-family: \"JetBrains Mono\", monospace; font-size: 0.9rem; color: #e5383b;'>" + String(timeStr) + "</div>");
        client.print("<div style='font-size: 1.2rem; color: #b1a7a6;'>");
        
        // Weather icon for hourly forecast
        String hourlyIcon = "fa-cloud";
        if (condition.indexOf("Clear") >= 0) {
            hourlyIcon = "fa-sun";
        } else if (condition.indexOf("Cloud") >= 0) {
            hourlyIcon = "fa-cloud";
        } else if (condition.indexOf("Rain") >= 0) {
            hourlyIcon = "fa-cloud-rain";
        } else if (condition.indexOf("Snow") >= 0) {
            hourlyIcon = "fa-snowflake";
        } else if (condition.indexOf("Thunder") >= 0) {
            hourlyIcon = "fa-bolt";
        }
        
        client.print("<i class='fas " + hourlyIcon + "'></i>");
        client.print("</div>");
        client.print("</div>");
        client.print("<div style='text-align: right;'>");
        client.print("<div style='font-family: \"JetBrains Mono\", monospace; font-weight: 600; color: #e5383b;'>" + String((int)temp) + "°F</div>");
        client.print("<div style='font-size: 0.8rem; color: #b1a7a6;'>" + String(humidity) + "% " + String(windSpeed, 1) + "mph</div>");
        client.print("</div>");
        client.print("</div>");
      }
    } else {
      client.print("<div style='text-align: center; color: #b1a7a6; padding: 20px;'>No hourly data available</div>");
    }
    
    client.print("</div>");
    client.print("</div>");
    
    // System Status Card
    client.print("<div class='card'>");
    client.print("<h2><i class='fas fa-server'></i> System Status</h2>");
    client.print("<div style='display: grid; gap: 12px;'>");
    client.print("<div style='display: flex; justify-content: space-between; align-items: center;'>");
    client.print("<span style='color: #b1a7a6;'>WiFi Connection:</span>");
    client.print("<span><span class='status-indicator " + String(WiFi.status() == WL_CONNECTED ? "status-online" : "status-offline") + "'></span>" + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "</span>");
    client.print("</div>");
    client.print("<div style='display: flex; justify-content: space-between; align-items: center;'>");
    client.print("<span style='color: #b1a7a6;'>IP Address:</span>");
    client.print("<span>" + WiFi.localIP().toString() + "</span>");
    client.print("</div>");
    client.print("<div style='display: flex; justify-content: space-between; align-items: center;'>");
    client.print("<span style='color: #b1a7a6;'>Signal Strength:</span>");
    client.print("<span>" + String(WiFi.RSSI()) + " dBm</span>");
    client.print("</div>");
    client.print("<div style='display: flex; justify-content: space-between; align-items: center;'>");
    client.print("<span style='color: #b1a7a6;'>Last Weather Update:</span>");
    client.print("<span>" + String((millis() - lastWeatherUpdate) / 1000) + "s ago</span>");
    client.print("</div>");
    client.print("</div>");
    client.print("</div>");
    
    client.print("</div>"); // End grid
    
    // Settings Forms
    client.print("<div class='grid'>");
    
    // Alarm Settings
    client.print("<div class='card'>");
    client.print("<h2><i class='fas fa-bell'></i> Alarm Settings</h2>");
    client.print("<form method='POST' action='/setalarm'>");
    client.print("<div class='form-group'>");
    client.print("<label>Hour:</label>");
    client.print("<input type='number' name='hour' value='" + String(alarmHour) + "' min='0' max='23'>");
    client.print("</div>");
    client.print("<div class='form-group'>");
    client.print("<label>Minute:</label>");
    client.print("<input type='number' name='minute' value='" + String(alarmMinute) + "' min='0' max='59'>");
    client.print("</div>");
    client.print("<div class='checkbox-group'>");
    client.print("<input type='checkbox' name='enabled' id='alarm-enabled' " + String(alarmEnabled ? "checked" : "") + ">");
    client.print("<label for='alarm-enabled'>Alarm Enabled</label>");
    client.print("</div>");
    client.print("<button type='submit' class='btn'>Set Alarm</button>");
    client.print("</form>");
    client.print("</div>");
    
    // Location Settings
    client.print("<div class='card'>");
    client.print("<h2><i class='fas fa-map-marker-alt'></i> Location Settings</h2>");
    client.print("<form method='POST' action='/setlocation'>");
    client.print("<div class='form-group'>");
    client.print("<label>City:</label>");
    client.print("<input type='text' name='city' value='" + weatherCity + "' placeholder='Enter city name'>");
    client.print("</div>");
    client.print("<div class='form-group'>");
    client.print("<label>Country Code:</label>");
    client.print("<input type='text' name='country' value='US' maxlength='2' placeholder='US'>");
    client.print("</div>");
    client.print("<div class='form-group'>");
    client.print("<label>Timezone:</label>");
    client.print("<input type='text' name='tz' value='America/Los_Angeles' placeholder='e.g., America/Los_Angeles'>");
    client.print("</div>");
    client.print("<button type='submit' class='btn'>Update Location</button>");
    client.print("</form>");
    client.print("</div>");
    
    // Other Settings
    client.print("<div class='card'>");
    client.print("<h2><i class='fas fa-cog'></i> Other Settings</h2>");
    client.print("<form method='POST' action='/setsettings'>");
    client.print("<div class='checkbox-group'>");
    client.print("<input type='checkbox' name='beep' id='beep-enabled' " + String(beepEnabled ? "checked" : "") + ">");
    client.print("<label for='beep-enabled'>Button Beep Sound</label>");
    client.print("</div>");
    client.print("<div class='checkbox-group'>");
    client.print("<input type='checkbox' name='24hr' id='24hr-format' " + String(use24hr ? "checked" : "") + ">");
    client.print("<label for='24hr-format'>24-Hour Time Format</label>");
    client.print("</div>");
    #if HAS_MATRIX
    client.print("<div class='checkbox-group'>");
    client.print("<input type='checkbox' name='led' id='led-matrix' " + String(ledMatrixEnabled ? "checked" : "") + ">");
    client.print("<label for='led-matrix'>LED Matrix Display</label>");
    client.print("</div>");
    #endif
    client.print("<button type='submit' class='btn'>Update Settings</button>");
    client.print("</form>");
    client.print("</div>");
    
    client.print("</div>"); // End settings grid
    
    client.print("</div>"); // End container
    client.print("</body></html>");
  } else if (request.indexOf("POST /setalarm") >= 0) {
    Serial.println("*** ALARM POST DETECTED ***");
    // Parse POST data for alarm
    
    // Parse form data
    int hourPos = postBody.indexOf("hour=");
    int minutePos = postBody.indexOf("minute=");
    int enabledPos = postBody.indexOf("enabled=on");
    
    // Debug: print the POST data
    Serial.println("POST data for alarm: " + postBody);
    Serial.print("hourPos: "); Serial.println(hourPos);
    Serial.print("minutePos: "); Serial.println(minutePos);
    Serial.print("enabledPos: "); Serial.println(enabledPos);
    
    if (hourPos >= 0 && minutePos >= 0) {
      String hourStr = postBody.substring(hourPos + 5, postBody.indexOf("&", hourPos) >= 0 ? postBody.indexOf("&", hourPos) : postBody.length());
      String minuteStr = postBody.substring(minutePos + 7, postBody.indexOf("&", minutePos) >= 0 ? postBody.indexOf("&", minutePos) : postBody.length());
      
      // URL decode
      hourStr = urlDecode(hourStr);
      minuteStr = urlDecode(minuteStr);
      
      Serial.print("hourStr: "); Serial.println(hourStr);
      Serial.print("minuteStr: "); Serial.println(minuteStr);
      
      alarmHour = hourStr.toInt();
      alarmMinute = minuteStr.toInt();
      alarmEnabled = (enabledPos >= 0);
      
      Serial.print("alarmHour: "); Serial.println(alarmHour);
      Serial.print("alarmMinute: "); Serial.println(alarmMinute);
      Serial.print("alarmEnabled: "); Serial.println(alarmEnabled);
      
      saveAlarmToEEPROM();
    }
    // Redirect back to main page
    client.println("HTTP/1.1 303 See Other");
    client.println("Location: /");
    client.println();
  } else if (request.indexOf("POST /setlocation") >= 0) {
    // Parse POST data for location
    
    int cityPos = postBody.indexOf("city=");
    int countryPos = postBody.indexOf("country=");
    int tzPos = postBody.indexOf("tz=");
    
    if (cityPos >= 0 && countryPos >= 0 && tzPos >= 0) {
      String city = postBody.substring(cityPos + 5, postBody.indexOf("&", cityPos) >= 0 ? postBody.indexOf("&", cityPos) : postBody.length());
      String country = postBody.substring(countryPos + 8, postBody.indexOf("&", countryPos) >= 0 ? postBody.indexOf("&", countryPos) : postBody.length());
      String tz = postBody.substring(tzPos + 3, postBody.indexOf("&", tzPos) >= 0 ? postBody.indexOf("&", tzPos) : postBody.length());
      
      // URL decode
      city = urlDecode(city);
      country = urlDecode(country);
      tz = urlDecode(tz);
      
      cityQuery = city + "," + country;
      saveLocationToEEPROM(city, country, tz);
      parseTimezoneAndSetOffset(tz);
      requestWeatherUpdate(); // Refresh weather
    }
    // Redirect back to main page
    client.println("HTTP/1.1 303 See Other");
    client.println("Location: /");
    client.println();
  } else if (request.indexOf("POST /setsettings") >= 0) {
    Serial.println("*** SETTINGS POST DETECTED ***");
    
    // Parse POST data for settings
    
    // Debug: print the POST data
    Serial.println("POST data for settings: " + postBody);
    
    int beepPos = postBody.indexOf("beep=on");
    int hr24Pos = postBody.indexOf("24hr=on");
    #if HAS_MATRIX
    int ledPos = postBody.indexOf("led=on");
    #endif
    
    // Debug: print the positions
    Serial.print("beepPos: "); Serial.println(beepPos);
    Serial.print("hr24Pos: "); Serial.println(hr24Pos);
    #if HAS_MATRIX
    Serial.print("ledPos: "); Serial.println(ledPos);
    #endif
    
    beepEnabled = (beepPos >= 0);
    use24hr = (hr24Pos >= 0);
    #if HAS_MATRIX
    ledMatrixEnabled = (ledPos >= 0);
    #endif
    
    // Debug: print the final values
    Serial.print("beepEnabled: "); Serial.println(beepEnabled);
    Serial.print("use24hr: "); Serial.println(use24hr);
    #if HAS_MATRIX
    Serial.print("ledMatrixEnabled: "); Serial.println(ledMatrixEnabled);
    #endif
    
    saveAlarmToEEPROM();
    // Redirect back to main page
    client.println("HTTP/1.1 303 See Other");
    client.println("Location: /");
    client.println();
  } else if (request.indexOf("GET /refresh") >= 0) {
    // Manual weather refresh
    requestWeatherUpdate();
    // Reset the last weather update timer for manual refresh
    lastWeatherUpdate = millis();
    // Redirect back to main page
    client.println("HTTP/1.1 303 See Other");
    client.println("Location: /");
    client.println();
  } else if (request.indexOf("GET /test") >= 0) {
    // Simple test endpoint
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println();
    client.print("<html><body>");
    client.print("<h1>Test Page</h1>");
    client.print("<p>Web server is working!</p>");
    client.print("<p>Current time: " + String(millis()) + "</p>");
    client.print("<form method='POST' action='/test'>");
    client.print("<input type='checkbox' name='test' value='on'> Test Checkbox<br>");
    client.print("<input type='submit' value='Test Submit'>");
    client.print("</form>");
    client.print("<p><a href='/'>Back to Dashboard</a></p>");
    client.print("</body></html>");
  } else if (request.indexOf("POST /test") >= 0) {
    // Test POST endpoint
    Serial.println("*** TEST POST DETECTED ***");
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println();
    client.print("<html><body>");
    client.print("<h1>Test POST Result</h1>");
    client.print("<p>POST data: " + postBody + "</p>");
    client.print("<p><a href='/test'>Back to Test</a></p>");
    client.print("<p><a href='/'>Back to Dashboard</a></p>");
    client.print("</body></html>");
  } else if (request.indexOf("POST") >= 0) {
    // Catch any POST request that doesn't match specific handlers
    Serial.println("*** UNHANDLED POST REQUEST ***");
    Serial.println(request);
    client.println("HTTP/1.1 404 Not Found");
    client.println("Content-type:text/html");
    client.println();
    client.print("POST endpoint not found");
  } else {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Content-type:text/html");
    client.println();
    client.print("Not Found");
  }
}

// --- Web Server Handling (Legacy - now handled by processWebRequests) ---
void handleWebRequests() {
  // This function is now deprecated - web requests are handled by processWebRequests()
}
