#define BLYNK_TEMPLATE_ID "TMPL6xytI8GQH"
#define BLYNK_TEMPLATE_NAME "PET FEEDER"
#define BLYNK_AUTH_TOKEN "jNOji09kF2W8YYSWkvKZF_Gdl0WsYi8x"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <TimeLib.h>
#include <WidgetRTC.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

// Authentication and network details
char auth[] = BLYNK_AUTH_TOKEN;
char ssid[32] = "ZTE_2.4G_XGTcbM"; // Changed to array to be modifiable
char pass[32] = "eCumcmHa";       // Changed to array to be modifiable

// SoftAP Config
const char* apSSID = "PetFeeder_Setup";
const char* apPassword = ""; // You can change this or leave it empty for an open network
bool configMode = false;
int configTimeout = 0;
const int CONFIG_TIMEOUT_MAX = 300; // 5 minutes timeout for config mode

// DNS Server for captive portal
DNSServer dnsServer;
ESP8266WebServer webServer(80);

// Pin Definitions for A4988 Driver
const int STEP_PIN = D1;  // Step pin
const int DIR_PIN = D2;   // Direction pin
const int EN_PIN = D4;    // Enable pin
const int MS1_PIN = D5;   // Microstepping pin 1
const int MS2_PIN = D6;   // Microstepping pin 2
const int MS3_PIN = D7;   // Microstepping pin 3
const int CONFIG_PIN = D3; // Button to enter config mode

// EEPROM addresses
const int EEPROM_SSID_ADDR = 0;
const int EEPROM_PASS_ADDR = 32;
const int EEPROM_VALID_ADDR = 96;

// Feeding Parameters
const int STEPS_PER_ROTATION = 200;  // NEMA17 standard (1.8° per step)
int feedingAmount = 50;              // Default feeding amount (steps)
bool feedingInProgress = false;

// Time-based feeding
unsigned long lastFeedTime = 0; // Last time fed (in seconds since midnight)
bool scheduleEnabled = true;
unsigned long nextFeedTime = 0; // Next scheduled feed time (in seconds since midnight)
unsigned long lastFeedTimestamp = 0; // For safety timeout between feeds

// Store feeding times
const int MAX_FEEDING_TIMES = 4;
unsigned long scheduledFeedingTimes[MAX_FEEDING_TIMES] = {0}; // In seconds since midnight
bool feedingTimeEnabled[MAX_FEEDING_TIMES] = {false};
int activeScheduleCount = 0;

// Track the last feeding period to prevent multiple feeds
unsigned long lastFeedingPeriod = 0;

BlynkTimer timer;
WidgetRTC rtc;

// Function prototypes
void feedNow();
void checkScheduledFeeding();
void updateStatus();
String getCurrentTime();
String formatTimeFromSeconds(unsigned long seconds);
unsigned long getCurrentTimeInSeconds();
void startConfigPortal();
void handleRoot();
void handleSave();
void handleFeed();
void handleNotFound();
bool captivePortal();
bool tryConnect();
void loadCredentials();
void saveCredentials();
void updateNextFeedTime();

// This function will be called when device connects to Blynk server
BLYNK_CONNECTED() {
  // Synchronize time on connection
  rtc.begin();
  Blynk.syncVirtual(V1); // Sync portion size slider
  Blynk.syncVirtual(V4); // Sync schedule enabled/disabled
  Blynk.syncVirtual(V7, V8, V9, V10); // Sync feeding schedule times
  Serial.println("Blynk connected and RTC synced");
  
  // Calculate initial next feed time
  updateNextFeedTime();
}

// Feed now button
BLYNK_WRITE(V0) {
  int buttonValue = param.asInt();
  Serial.print("Feed Now button pressed: ");
  Serial.println(buttonValue);
  
  if (buttonValue == 1) {
    Serial.println("Triggering feedNow function");
    feedNow();
    
    // Record the feeding time
    lastFeedTime = getCurrentTimeInSeconds();
    updateNextFeedTime();
  }
}

// Adjust portion size slider
BLYNK_WRITE(V1) {
  feedingAmount = param.asInt() * 100;  // Scale slider value (0-10) to steps
  Serial.print("Feeding amount set to: ");
  Serial.println(feedingAmount);
  Blynk.virtualWrite(V5, String("Portion: ") + String(param.asInt()));
}

// Schedule enable/disable switch
BLYNK_WRITE(V4) {
  scheduleEnabled = param.asInt();
  Serial.print("Schedule enabled: ");
  Serial.println(scheduleEnabled);
  updateStatus(); // Update status display when schedule is toggled
}

// Set scheduled feeding times (V7, V8, V9, V10 for up to 4 feeding times)
BLYNK_WRITE(V7) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    int hours = t.getStartHour();
    int minutes = t.getStartMinute();
    scheduledFeedingTimes[0] = hours * 3600 + minutes * 60;
    feedingTimeEnabled[0] = true;
    Serial.print("Schedule 1 set to: ");
    Serial.print(hours);
    Serial.print(":");
    if (minutes < 10) Serial.print("0");
    Serial.println(minutes);
    updateNextFeedTime();
    updateStatus();
  } else {
    feedingTimeEnabled[0] = false;
    updateNextFeedTime();
    updateStatus();
  }
}

BLYNK_WRITE(V8) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    int hours = t.getStartHour();
    int minutes = t.getStartMinute();
    scheduledFeedingTimes[1] = hours * 3600 + minutes * 60;
    feedingTimeEnabled[1] = true;
    Serial.print("Schedule 2 set to: ");
    Serial.print(hours);
    Serial.print(":");
    if (minutes < 10) Serial.print("0");
    Serial.println(minutes);
    updateNextFeedTime();
    updateStatus();
  } else {
    feedingTimeEnabled[1] = false;
    updateNextFeedTime();
    updateStatus();
  }
}

BLYNK_WRITE(V9) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    int hours = t.getStartHour();
    int minutes = t.getStartMinute();
    scheduledFeedingTimes[2] = hours * 3600 + minutes * 60;
    feedingTimeEnabled[2] = true;
    Serial.print("Schedule 3 set to: ");
    Serial.print(hours);
    Serial.print(":");
    if (minutes < 10) Serial.print("0");
    Serial.println(minutes);
    updateNextFeedTime();
    updateStatus();
  } else {
    feedingTimeEnabled[2] = false;
    updateNextFeedTime();
    updateStatus();
  }
}

BLYNK_WRITE(V10) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    int hours = t.getStartHour();
    int minutes = t.getStartMinute();
    scheduledFeedingTimes[3] = hours * 3600 + minutes * 60;
    feedingTimeEnabled[3] = true;
    Serial.print("Schedule 4 set to: ");
    Serial.print(hours);
    Serial.print(":");
    if (minutes < 10) Serial.print("0");
    Serial.println(minutes);
    updateNextFeedTime();
    updateStatus();
  } else {
    feedingTimeEnabled[3] = false;
    updateNextFeedTime();
    updateStatus();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nESP8266 Pet Feeder Starting...");
  
  // Initialize EEPROM
  EEPROM.begin(512);
  
  // Set up config button pin
  pinMode(CONFIG_PIN, INPUT_PULLUP);
  
  // Set up A4988 driver pins
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(EN_PIN, OUTPUT);
  pinMode(MS1_PIN, OUTPUT);
  pinMode(MS2_PIN, OUTPUT);
  pinMode(MS3_PIN, OUTPUT);
  
  Serial.println("GPIO pins configured");
  
  // Set microstepping mode (1/8 step)
  digitalWrite(MS1_PIN, HIGH);
  digitalWrite(MS2_PIN, HIGH);
  digitalWrite(MS3_PIN, LOW);
  
  // Enable the stepper motor driver (active LOW)
  digitalWrite(EN_PIN, LOW);
  
  // Set direction (clockwise)
  digitalWrite(DIR_PIN, HIGH);
  
  Serial.println("Driver configuration complete");
  
  // Test motor with a quick movement
  Serial.println("Testing motor with quick movement...");
  for(int i = 0; i < 10; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(1000);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(1000);
  }
  Serial.println("Motor test complete");
  
  // Load credentials from EEPROM
  loadCredentials();
  
  // Check if config button is pressed during boot or if no valid credentials
  if (digitalRead(CONFIG_PIN) == LOW || EEPROM.read(EEPROM_VALID_ADDR) != 1) {
    Serial.println("Config button pressed or no valid credentials - starting config portal");
    startConfigPortal();
  } else {
    // Try to connect to WiFi
    if (!tryConnect()) {
      Serial.println("Failed to connect to WiFi - starting config portal");
      startConfigPortal();
    } else {
      // Connected to WiFi, setup Blynk
      Serial.println("Connected to WiFi, initializing Blynk...");
      Blynk.config(auth);
      
      // Try to connect to Blynk with multiple attempts
      int connectionAttempts = 0;
      while (!Blynk.connect() && connectionAttempts < 3) {
        connectionAttempts++;
        Serial.print("Blynk connection attempt ");
        Serial.print(connectionAttempts);
        Serial.println("/3 failed");
        delay(1000);
      }
      
      if (Blynk.connected()) {
        Serial.println("Connected to Blynk server");
        rtc.begin(); // Make sure RTC is initialized
        Blynk.syncAll(); // Sync all widget states
        
        // Initialize the last feed time to current time
        lastFeedTime = getCurrentTimeInSeconds();
        updateNextFeedTime();
      } else {
        Serial.println("Failed to connect to Blynk server after multiple attempts");
      }
      
      // Check for scheduled feedings every minute
      timer.setInterval(60000L, checkScheduledFeeding);
      
      // Update status display every 5 seconds
      timer.setInterval(5000L, updateStatus);
      
      Serial.println("Pet Feeder Ready!");
      Blynk.virtualWrite(V5, "Pet Feeder Ready!");
    }
  }
}

void loop() {
  if (configMode) {
    // Handle DNS and WebServer in config mode
    dnsServer.processNextRequest();
    webServer.handleClient();
    
    // Check if configuration timeout has been reached
    if (configTimeout > 0) {
      configTimeout--;
      if (configTimeout == 0) {
        Serial.println("Config timeout reached, restarting...");
        ESP.restart();
      }
      delay(1000); // 1 second timer for timeout
    }
  } else {
    // Normal operation mode
    Blynk.run();
    timer.run();
    
    // Check if config button is pressed during normal operation
    if (digitalRead(CONFIG_PIN) == LOW) {
      delay(50); // Debounce
      if (digitalRead(CONFIG_PIN) == LOW) {
        unsigned long pressStart = millis();
        while (digitalRead(CONFIG_PIN) == LOW && (millis() - pressStart < 3000)) {
          delay(10);
        }
        
        // If button was held for 3 seconds, enter config mode
        if (millis() - pressStart >= 3000) {
          Serial.println("Config button held for 3 seconds - starting config portal");
          startConfigPortal();
        }
      }
    }
  }
}

void feedNow() {
  // Safety check: don't feed if we've fed within the last 3 minutes
  unsigned long currentMillis = millis();
  if (currentMillis - lastFeedTimestamp < 180000 && lastFeedTimestamp > 0) { // 3 minutes in milliseconds
    Serial.println("Feeding skipped - too soon after last feeding (safety timeout)");
    return;
  }
  
  if (feedingInProgress) {
    Serial.println("Feeding already in progress, skipping request");
    return;
  }
  
  Serial.println("Starting feeding sequence");
  feedingInProgress = true;
  if (!configMode) {
    Blynk.virtualWrite(V5, "Feeding in progress...");
  }
  
  // Enable stepper
  digitalWrite(EN_PIN, LOW);
  Serial.println("Driver enabled");
  
  // Calculate rotations from feeding amount
  int numRotations = max(1, feedingAmount / 20); // Convert steps to rotations, minimum 1
  Serial.print("Running motor for ");
  Serial.print(numRotations);
  Serial.println(" rotations");
  
  // Run multiple complete rotations
  for (int rotation = 0; rotation < numRotations; rotation++) {
    Serial.print("Rotation ");
    Serial.print(rotation + 1);
    Serial.print(" of ");
    Serial.println(numRotations);
    
    for (int step = 0; step < STEPS_PER_ROTATION; step++) {
      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(1000);
      digitalWrite(STEP_PIN, LOW);
      delayMicroseconds(1000);
      
      // Print progress periodically
      if (step % 50 == 0) {
        Serial.print("  Step ");
        Serial.print(step);
        Serial.print(" of ");
        Serial.println(STEPS_PER_ROTATION);
      }
    }
    
    // Small pause between rotations
    delay(100);
  }
  
  // Disable stepper to save power
  digitalWrite(EN_PIN, HIGH);
  Serial.println("Driver disabled");
  
  feedingInProgress = false;
  
  if (!configMode) {
    String lastFedTime = getCurrentTime();
    Blynk.virtualWrite(V5, "Last fed: " + lastFedTime);
    Serial.println("Feeding complete at " + lastFedTime);
    
    // Update the last feed time and calculate the next one
    lastFeedTime = getCurrentTimeInSeconds();
    lastFeedTimestamp = millis();  // Record when we last fed
    updateNextFeedTime();
    updateStatus();
  } else {
    Serial.println("Feeding complete (config mode)");
  }
}

void updateNextFeedTime() {
  // Start by assuming no next feeding time
  bool foundNextTime = false;
  nextFeedTime = 0;
  activeScheduleCount = 0;
  
  // Get current time
  unsigned long currentTimeSeconds = getCurrentTimeInSeconds();
  
  // Find the next upcoming feeding time from our scheduled times
  for (int i = 0; i < MAX_FEEDING_TIMES; i++) {
    if (feedingTimeEnabled[i]) {
      activeScheduleCount++;
      
      // If this schedule time is in the future today, consider it
      if (scheduledFeedingTimes[i] > currentTimeSeconds) {
        if (!foundNextTime || scheduledFeedingTimes[i] < nextFeedTime) {
          nextFeedTime = scheduledFeedingTimes[i];
          foundNextTime = true;
        }
      }
    }
  }
  
  // If no future feeding time found today, find the earliest one for tomorrow
  if (!foundNextTime && activeScheduleCount > 0) {
    nextFeedTime = 24 * 3600; // Set to maximum value (end of day)
    
    for (int i = 0; i < MAX_FEEDING_TIMES; i++) {
      if (feedingTimeEnabled[i] && scheduledFeedingTimes[i] < nextFeedTime) {
        nextFeedTime = scheduledFeedingTimes[i];
        foundNextTime = true;
      }
    }
  }
  
  // Log the next feeding time
  if (foundNextTime) {
    Serial.print("Next feeding scheduled at: ");
    Serial.println(formatTimeFromSeconds(nextFeedTime));
  } else {
    Serial.println("No scheduled feeding times set.");
  }
}

void checkScheduledFeeding() {
  if (!scheduleEnabled || feedingInProgress || configMode || activeScheduleCount == 0) {
    return;
  }
  
  // Check if Blynk is connected
  if (!Blynk.connected()) {
    Serial.println("Skipping scheduled feeding check - Blynk not connected");
    return;
  }
  
  // Get current time from RTC
  unsigned long currentTimeSeconds = getCurrentTimeInSeconds();
  
  // Debug output
  Serial.print("Checking scheduled feeding at ");
  Serial.print(getCurrentTime());
  Serial.print(" (Next feed at: ");
  Serial.print(formatTimeFromSeconds(nextFeedTime));
  Serial.println(")");
  
  // Calculate the current time period in minutes (for tracking feeding periods)
  unsigned long currentFeedingPeriod = (hour() * 60) + minute();
  
  // Define a window of time when we consider it the "scheduled time"
  // Only dispense food if we're within 1 minute of the scheduled time
  bool isScheduledTimeWindow = false;
  
  // Check if we're within a small window of the next feed time
  if (currentTimeSeconds >= nextFeedTime && currentTimeSeconds < (nextFeedTime + 60)) {
    isScheduledTimeWindow = true;
  }
  
  // Only feed if:
  // 1. We're in the scheduled time window AND
  // 2. We haven't already fed during this specific minute of the day
  if (isScheduledTimeWindow && lastFeedingPeriod != currentFeedingPeriod) {
    Serial.println("Scheduled feeding time reached!");
    Serial.print("Last feeding period: ");
    Serial.print(lastFeedingPeriod);
    Serial.print(", Current period: ");
    Serial.println(currentFeedingPeriod);
    
    feedNow();
    
    // Update tracking variables
    lastFeedTime = currentTimeSeconds;
    lastFeedingPeriod = currentFeedingPeriod;
    
    // Calculate the next feeding time
    updateNextFeedTime();
  }
}

String getCurrentTime() {
  char timeStr[10];
  sprintf(timeStr, "%02d:%02d", hour(), minute());
  return String(timeStr);
}

String formatTimeFromSeconds(unsigned long totalSeconds) {
  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  char timeStr[10];
  sprintf(timeStr, "%02d:%02d", hours, minutes);
  return String(timeStr);
}

unsigned long getCurrentTimeInSeconds() {
  return hour() * 3600 + minute() * 60 + second();
}

void updateStatus() {
  if (configMode) return;
  
  // Debug info
  Serial.print("Current time from RTC: ");
  Serial.print(hour());
  Serial.print(":");
  if (minute() < 10) Serial.print("0");
  Serial.print(minute());
  Serial.print(":");
  if (second() < 10) Serial.print("0");
  Serial.println(second());
  
  // Update next feeding time on V2
  String nextFeeding = "Next: ";
  if (scheduleEnabled && activeScheduleCount > 0) {
    nextFeeding += formatTimeFromSeconds(nextFeedTime);
    
    // Calculate time remaining
    unsigned long currentTime = getCurrentTimeInSeconds();
    unsigned long timeRemaining;
    
    if (nextFeedTime > currentTime) {
      timeRemaining = nextFeedTime - currentTime;
    } else {
      // Feed time is tomorrow
      timeRemaining = (24 * 3600) - currentTime + nextFeedTime;
    }
    
    int hoursRemaining = timeRemaining / 3600;
    int minutesRemaining = (timeRemaining % 3600) / 60;
    
    nextFeeding += " (";
    if (hoursRemaining > 0) {
      nextFeeding += String(hoursRemaining) + "h ";
    }
    nextFeeding += String(minutesRemaining) + "m)";
  } else if (!scheduleEnabled) {
    nextFeeding += "Schedule OFF";
  } else {
    nextFeeding += "No times set";
  }
  
  // Update status widgets
  Blynk.virtualWrite(V2, nextFeeding);
  
  // Update V5 with current portion size and last fed time
  Blynk.virtualWrite(V5, String("Portion: ") + String(feedingAmount / 100) + 
                        " (Last: " + getCurrentTime() + ")");
  
  // Update V6 with active schedule count
  String scheduleStr = "Schedule: ";
  scheduleStr += String(activeScheduleCount) + " active times";
  Blynk.virtualWrite(V6, scheduleStr);
}

// SoftAP Configuration Functions

void startConfigPortal() {
  // Disconnect from current WiFi if connected
  WiFi.disconnect();
  
  // Create SoftAP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, apPassword);
  
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  
  // Setup the DNS server redirecting all domains to the AP IP
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", myIP);
  
  // Set up web server handlers
  webServer.on("/", handleRoot);
  webServer.on("/save", handleSave);
  webServer.on("/feed", HTTP_POST, handleFeed); // Add the handler
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  
  // Set config mode flag and timeout
  configMode = true;
  configTimeout = CONFIG_TIMEOUT_MAX;
  
  Serial.println("Configuration portal started");
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Pet Feeder Setup</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;text-align:center;background-color:#f8f9fa;}";
  html += ".container{max-width:400px;margin:0 auto;background-color:white;padding:20px;border-radius:10px;box-shadow:0 4px 6px rgba(0,0,0,0.1);}";
  html += "h1{color:#4CAF50;}";
  html += "input[type=text],input[type=password]{width:100%;padding:12px 20px;margin:8px 0;display:inline-block;border:1px solid #ccc;";
  html += "border-radius:4px;box-sizing:border-box;}";
  html += "button{background-color:#4CAF50;color:white;padding:14px 20px;margin:8px 0;border:none;cursor:pointer;width:100%;border-radius:4px;}";
  html += "button:hover{opacity:0.8;}";
  html += ".test-feed{background-color:#2196F3;}";
  html += "</style>";
  html += "</head><body><div class='container'>";
  html += "<h1>Pet Feeder WiFi Setup</h1>";
  html += "<form action='/save' method='post'>";
  html += "<label for='ssid'><b>WiFi Name (SSID)</b></label>";
  html += "<input type='text' name='ssid' id='ssid' value='" + String(ssid) + "' required>";
  html += "<label for='password'><b>WiFi Password</b></label>";
  html += "<input type='password' name='password' id='password' value='" + String(pass) + "' required>";
  html += "<button type='submit'>Save Configuration</button>";
  html += "</form>";
  html += "<button class='test-feed' onclick='testFeed()'>Test Feed Now</button>";
  html += "<p>Device will restart after saving configuration.</p>";
  html += "</div>";
  html += "<script>";
  html += "function testFeed() {";
  html += "  fetch('/feed', {method: 'POST'}).then(response => {";
  html += "    alert('Test feeding initiated!');";
  html += "  });";
  html += "}";
  html += "</script>";
  html += "</body></html>";
  
  webServer.send(200, "text/html", html);
}

void handleSave() {
  if (webServer.method() != HTTP_POST) {
    webServer.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  String newSSID = webServer.arg("ssid");
  String newPass = webServer.arg("password");
  
  // Validate inputs
  if (newSSID.length() == 0) {
    webServer.send(400, "text/plain", "SSID cannot be empty");
    return;
  }
  
  // Copy to global variables (with length checks)
  strncpy(ssid, newSSID.c_str(), sizeof(ssid) - 1);
  ssid[sizeof(ssid) - 1] = '\0'; // Ensure null termination
  
  strncpy(pass, newPass.c_str(), sizeof(pass) - 1);
  pass[sizeof(pass) - 1] = '\0'; // Ensure null termination
  
  // Save to EEPROM
  saveCredentials();
  
  // Send success response
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Pet Feeder Setup Complete</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;text-align:center;background-color:#f8f9fa;}";
  html += ".container{max-width:400px;margin:0 auto;background-color:white;padding:20px;border-radius:10px;box-shadow:0 4px 6px rgba(0,0,0,0.1);}";
  html += "h1{color:#4CAF50;}";
  html += "</style>";
  html += "<meta http-equiv='refresh' content='5;url=/'>";
  html += "</head><body><div class='container'>";
  html += "<h1>Settings Saved</h1>";
  html += "<p>WiFi credentials have been saved. The device will restart in 5 seconds.</p>";
  html += "</div></body></html>";
  
  webServer.send(200, "text/html", html);
  
  // Set a timer to restart the ESP
  Serial.println("Configuration saved, restarting in 5 seconds...");
  delay(5000);
  ESP.restart();
}

// Captive portal handler
void handleNotFound() {
  if (captivePortal()) {
    return;
  }
  
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += webServer.uri();
  message += "\nMethod: ";
  message += (webServer.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += webServer.args();
  message += "\n";
  for (uint8_t i = 0; i < webServer.args(); i++) {
    message += " " + webServer.argName(i) + ": " + webServer.arg(i) + "\n";
  }
  webServer.send(404, "text/plain", message);
}

// Special handler for the test feed function
void handleFeed() {
  feedNow();
  webServer.send(200, "text/plain", "Feeding initiated");
}

// Helper function for captive portal
bool captivePortal() {
  if (webServer.hostHeader() != WiFi.softAPIP().toString()) {
    webServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    webServer.send(302, "text/plain", "");
    return true;
  }
  return false;
}

// WiFi connection function
bool tryConnect() {
  Serial.println("Attempting to connect to WiFi...");
  Serial.print("SSID: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 20) { // 10 second timeout
    delay(500);
    Serial.print(".");
    attempt++;
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected, IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("Failed to connect to WiFi");
    return false;
  }
}

// EEPROM Functions
void loadCredentials() {
  // Check if we have valid credentials stored
  if (EEPROM.read(EEPROM_VALID_ADDR) == 1) {
    // Read SSID
    for (int i = 0; i < 32; i++) {
      ssid[i] = char(EEPROM.read(EEPROM_SSID_ADDR + i));
    }
    ssid[31] = '\0'; // Ensure null termination
    
    // Read password
    for (int i = 0; i < 32; i++) {
      pass[i] = char(EEPROM.read(EEPROM_PASS_ADDR + i));
    }
    pass[31] = '\0'; // Ensure null termination
    
    Serial.println("Loaded WiFi credentials from EEPROM");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("Password: ");
    Serial.print("********"); // Don't print actual password
  } else {
    Serial.println("No valid credentials found in EEPROM");
  }
}

void saveCredentials() {
  // Write SSID
  for (int i = 0; i < strlen(ssid); i++) {
    EEPROM.write(EEPROM_SSID_ADDR + i, ssid[i]);
  }
  // Null-terminate
  EEPROM.write(EEPROM_SSID_ADDR + strlen(ssid), 0);
  
  // Write password
  for (int i = 0; i < strlen(pass); i++) {
    EEPROM.write(EEPROM_PASS_ADDR + i, pass[i]);
  }
  // Null-terminate
  EEPROM.write(EEPROM_PASS_ADDR + strlen(pass), 0);
  
  // Mark as valid
  EEPROM.write(EEPROM_VALID_ADDR, 1);
  
  EEPROM.commit();
  Serial.println("Saved WiFi credentials to EEPROM");
}
