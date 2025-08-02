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
#include <Thread.h> // ArduinoThread library
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
String weatherCity       = "???";
String weatherTemp       = "N/A";
String weatherCondition  = "N/A";
volatile bool weatherDirty      = true;

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
const unsigned long displayOffDelay = 5000; // 5 seconds
bool displayJustTurnedOff = false; // Flag to prevent immediate turn-on
unsigned long displayTurnOffTime = 0; // When display was turned off

// Misc
int  tempAlarmHour   = 7;
int  tempAlarmMinute = 0;

// XOR encryption key for password
const char XOR_KEY = 0x5A;

// --- ArduinoThread for Weather ---
Thread weatherThread;
bool weatherFetchRequested = false;

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
void drawPaneWeather();
void drawProgressBar(int progress, int total, const String& message);
void fetchIP2LocationAndSetOffset();
void parseTimezoneAndSetOffset(const String& tzString);
void initNTP();
bool checkDNSResolution();
bool updateLocalTime();
void fetchCurrentWeather();
void performNetworkDiagnostics();
void saveLastKnownTime();
void loadLastKnownTime();

// --- Weather Thread Callback ---
void weatherThreadCallback() {
  if (weatherFetchRequested && !alarmSounding) {
    fetchCurrentWeather();
    weatherFetchRequested = false;
  }
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
  Serial.begin(115200);
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
} else if(updateLocalTime()){  
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Time sync success!");
      display.setCursor(0,10);
      display.println("Getting weather...");
      display.display();
      delay(500);
      
      Serial.println("NTP time set success"); 
      timeHasBeenSet=true; 
      ntpRetryCount = 0; // Reset retry count on success
      usingFallbackTime = false;
      saveLastKnownTime(); // Save the successful time
    } else { 
      Serial.println("NTP time set fail, trying last known time");
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Time sync failed");
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
      lastNTPRetry = millis(); // Start retry timer
    }
    weatherFetchRequested = true; // initial fetch
    lastWeatherUpdate = millis();
    lastNTPUpdate = millis();
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

  // Start weather "thread"
  weatherThread.onRun(weatherThreadCallback);
  weatherThread.setInterval(100);
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
      weatherFetchRequested = true;
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
      if(updateLocalTime()) {
        Serial.println(F("Manual sync successful!"));
        timeHasBeenSet = true;
      } else {
        Serial.println(F("Manual sync failed"));
      }
    } else if (cmd.equalsIgnoreCase("diagnostics")) {
      performNetworkDiagnostics();
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
    timeClient.update();
    lastNTPUpdate = millis();
    
    // Save time to EEPROM every 5 minutes when NTP is working
    static unsigned long lastTimeSave = 0;
    if(millis() - lastTimeSave > 300000) { // 5 minutes
      saveLastKnownTime();
      lastTimeSave = millis();
    }
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
    
    if(updateLocalTime()) {
      Serial.println("NTP retry successful");
      timeHasBeenSet = true;
      ntpRetryCount = 0;
      usingFallbackTime = false;
    } else {
      Serial.println("NTP retry failed");
      ntpRetryCount++;
      lastNTPRetry = millis();
      if(!usingFallbackTime) {
        fallbackStartTime = millis();
        usingFallbackTime = true;
      }
    }
  }
  
  // Periodic network health check (but not during alarm)
  if (WiFi.status() == WL_CONNECTED && !alarmSounding && millis() - lastNetworkCheck > networkCheckInterval) {
    performNetworkDiagnostics();
    lastNetworkCheck = millis();
  }

  // Weather update timer (threaded) - but not during alarm
  if (WiFi.status() == WL_CONNECTED && !alarmSounding && millis() - lastWeatherUpdate > weatherInterval && !weatherFetchRequested) {
    weatherFetchRequested = true;
    lastWeatherUpdate = millis();
  }
  // Only run weather thread if not alarming
  if (!alarmSounding) {
    weatherThread.run();
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
  weatherFetchRequested = false;
  
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
      if(weatherDirty) {
        drawPaneWeather();
        lastWeatherCity = weatherCity;
        lastWeatherTemp = weatherTemp;
        lastWeatherCondition = weatherCondition;
        weatherDirty = false;
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

void drawPaneWeather(){
  display.setTextSize(2);
  display.setCursor(0,16);
  display.print(weatherTemp);
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
  String cName = weatherCity.length()>10?weatherCity.substring(0,10)+"..":weatherCity;
  display.print(cName);
  display.setCursor(left,24);
  String cond = weatherCondition.length()>10?weatherCondition.substring(0,10)+"..":weatherCondition;
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

// Enhanced network diagnostics
void performNetworkDiagnostics() {
  if(!networkDiagnosticsEnabled) return;
  
  Serial.println("=== Network Diagnostics ===");
  Serial.print("WiFi Status: ");
  Serial.println(WiFi.status());
  Serial.print("Signal Strength: ");
  Serial.println(WiFi.RSSI());
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("DNS: ");
  Serial.println(WiFi.dnsIP());
  
  // Test basic connectivity
  WiFiClient client;
  if(client.connect("8.8.8.8", 80)) {
    Serial.println("Basic internet connectivity: OK");
    client.stop();
  } else {
    Serial.println("Basic internet connectivity: FAILED");
  }
  
  Serial.println("==========================");
}

// Enhanced DNS resolution check with multiple fallbacks
bool checkDNSResolution() {
  const char* testHosts[] = {
    "time.google.com", 
    "8.8.8.8",  // Google DNS
    "1.1.1.1"   // Cloudflare DNS
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

void fetchCurrentWeather(){
  if(WiFi.status()!=WL_CONNECTED){
    weatherCity="???"; weatherTemp="N/A"; weatherCondition="NoWiFi"; weatherDirty=true; return;
  }
  WiFiClient client;
  String q = cityQuery; q.replace(" ", "%20");
  String host="api.openweathermap.org";
  String path="/data/2.5/weather?q="+q+"&appid="+apiKey+"&units="+units+"&lang="+language;
  Serial.print("Fetching OWM: "); Serial.print(host); Serial.println(path);
  if(!client.connect(host.c_str(),80)){
    Serial.println("OWM connect fail"); return;
  }
  client.print("GET "+path+" HTTP/1.1\r\n"
               "Host: "+host+"\r\n"
               "User-Agent: Arduino\r\n"
               "Connection: close\r\n\r\n");
  unsigned long t0=millis();
  while(!client.available() && client.connected()){
    if(millis()-t0>10000){
      Serial.println("OWM header timeout");
      client.stop();
      return;
    }
    delay(10);
  }
  while(client.connected()){
    String line = client.readStringUntil('\n');
    if(line.length()<=1) break;
  }
  String payload;
  while(client.available()) payload+=client.readString();
  client.stop();
  Serial.println("OWM JSON:\n"+payload);

  DynamicJsonDocument doc(768);
  DeserializationError err=deserializeJson(doc,payload);
  if(err){
    Serial.print("OWM parse err: "); Serial.println(err.f_str()); return;
  }
  float t=doc["main"]["temp"] | 0.0;
  String c=doc["weather"][0]["main"] | "";
  String n=doc["name"] | "???";
  weatherCity=n; weatherTemp=String((int)t); weatherCondition=c; weatherDirty=true;
}
