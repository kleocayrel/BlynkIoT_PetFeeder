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

BlynkTimer statusUpdateTimer;  // Create a timer for batching status updates
bool statusUpdateNeeded = false;
String nextFeedingStatus = "";
String lastFedStatus = "";
String scheduleStatus = "";

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
void setupBatchedUpdates();
void sendBatchedStatusUpdate();

// This function will be called when device connects to Blynk server
BLYNK_CONNECTED() {
  // Synchronize time on connection
  rtc.begin();
  // Perform a single sync request with multiple pins instead of individual calls
  Blynk.syncVirtual(V1, V4, V7, V8, V9, V10); // Sync all required values in one call
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
    // Don't call updateStatus() directly - let the timer handle it
    statusUpdateNeeded = true;
  }
}

// Adjust portion size slider
BLYNK_WRITE(V1) {
  feedingAmount = param.asInt() * 10;  // Scale slider value (0-10) to steps
  Serial.print("Feeding amount set to: ");
  Serial.println(feedingAmount);
  
  // Instead of immediately writing to Blynk
  lastFedStatus = String("Portion: ") + String(param.asInt()) + 
                  " (Last: " + getCurrentTime() + ")";
  statusUpdateNeeded = true;
}

// Schedule enable/disable switch
BLYNK_WRITE(V4) {
  scheduleEnabled = param.asInt();
  Serial.print("Schedule enabled: ");
  Serial.println(scheduleEnabled);
  statusUpdateNeeded = true; // Flag for update rather than immediate update
}

// Set scheduled feeding times (V7, V8, V9, V10 for up to 4 feeding times)
// Optimized version for all time inputs to reduce code duplication
void handleTimeInputChange(int scheduleIndex, const BlynkParam& param) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    int hours = t.getStartHour();
    int minutes = t.getStartMinute();
    scheduledFeedingTimes[scheduleIndex] = hours * 3600 + minutes * 60;
    feedingTimeEnabled[scheduleIndex] = true;
    Serial.print("Schedule ");
    Serial.print(scheduleIndex + 1);
    Serial.print(" set to: ");
    Serial.print(hours);
    Serial.print(":");
    if (minutes < 10) Serial.print("0");
    Serial.println(minutes);
  } else {
    feedingTimeEnabled[scheduleIndex] = false;
    Serial.print("Schedule ");
    Serial.print(scheduleIndex + 1);
    Serial.println(" disabled");
  }
  
  updateNextFeedTime();
  statusUpdateNeeded = true; // Flag for update instead of direct call
}

BLYNK_WRITE(V7) {
  handleTimeInputChange(0, param);
}

BLYNK_WRITE(V8) {
  handleTimeInputChange(1, param);
}

BLYNK_WRITE(V9) {
  handleTimeInputChange(2, param);
}

BLYNK_WRITE(V10) {
  handleTimeInputChange(3, param);
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
      
      // Setup batched updates instead of frequent status updates
      setupBatchedUpdates();
      
      Serial.println("Pet Feeder Ready!");
      lastFedStatus = "Pet Feeder Ready!";
      statusUpdateNeeded = true;
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
    statusUpdateTimer.run(); // Run the batched status update timer
    
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
    // Instead of immediately writing to Blynk, just update local status
    lastFedStatus = "Feeding in progress...";
    statusUpdateNeeded = true;
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
    // Update local status instead of writing to Blynk immediately
    lastFedStatus = "Last fed: " + lastFedTime;
    Serial.println("Feeding complete at " + lastFedTime);
    
    // Update the last feed time and calculate the next one
    lastFeedTime = getCurrentTimeInSeconds();
    lastFeedTimestamp = millis();  // Record when we last fed
    updateNextFeedTime();
    
    // Flag that we need to update status
    statusUpdateNeeded = true;
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
  
  // Signal that status needs update
  statusUpdateNeeded = true;
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
  
  // Only dispense food if we're within 1 minute of the scheduled time
  bool isScheduledTimeWindow = false;
  
  // Check if we're within a small window of the next feed time
  if (currentTimeSeconds >= nextFeedTime && currentTimeSeconds < (nextFeedTime + 60)) {
    isScheduledTimeWindow = true;
  }
  
  
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
    
    // Status update is already flagged in feedNow()
  }
}

// Update status but only send to Blynk when needed
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
  
  // Prepare next feeding time status
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
  
  // Update last fed status
  String lastFed = String("Portion: ") + String(feedingAmount / 100) + 
                   " (Last: " + getCurrentTime() + ")";
  
  // Update schedule status
  String scheduleStr = "Schedule: ";
  scheduleStr += String(activeScheduleCount) + " active times";
  
  // Only update Blynk if values have changed
  if (nextFeedingStatus != nextFeeding || 
      lastFedStatus != lastFed || 
      scheduleStatus != scheduleStr ||
      statusUpdateNeeded) {
      
    // Now send all updates as a batch
    Blynk.virtualWrite(V2, nextFeeding);
    Blynk.virtualWrite(V5, lastFed);
    Blynk.virtualWrite(V6, scheduleStr);
    
    // Save current status for comparison later
    nextFeedingStatus = nextFeeding;
    lastFedStatus = lastFed;
    scheduleStatus = scheduleStr;
    statusUpdateNeeded = false;
    
    Serial.println("Status updated to Blynk");
  }
}

// IMPLEMENTING MISSING FUNCTIONS

// Get current time as formatted string (HH:MM)
String getCurrentTime() {
  char timeStr[6]; // HH:MM\0
  sprintf(timeStr, "%02d:%02d", hour(), minute());
  return String(timeStr);
}

// Format seconds into time string
String formatTimeFromSeconds(unsigned long seconds) {
  int h = (seconds / 3600) % 24;
  int m = (seconds % 3600) / 60;
  
  char timeStr[6]; // HH:MM\0
  sprintf(timeStr, "%02d:%02d", h, m);
  return String(timeStr);
}

// Get current time in seconds since midnight
unsigned long getCurrentTimeInSeconds() {
  return hour() * 3600 + minute() * 60 + second();
}

// Setup batched updates to reduce Blynk traffic
void setupBatchedUpdates() {
  // Update status every 10 seconds if needed
  statusUpdateTimer.setInterval(10000L, sendBatchedStatusUpdate);
}

// Send batched status update if needed
void sendBatchedStatusUpdate() {
  if (statusUpdateNeeded) {
    updateStatus();
  }
}

// WiFi Configuration Functions
bool tryConnect() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  // Connect with a timeout
  WiFi.begin(ssid, pass);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed");
    return false;
  }
}

// Load WiFi credentials from EEPROM
void loadCredentials() {
  Serial.println("Loading credentials from EEPROM");
  
  // Check if EEPROM has valid data
  if (EEPROM.read(EEPROM_VALID_ADDR) != 1) {
    Serial.println("No valid credentials in EEPROM");
    return;
  }
  
  // Read SSID
  for (int i = 0; i < 32; i++) {
    ssid[i] = char(EEPROM.read(EEPROM_SSID_ADDR + i));
  }
  
  // Read password
  for (int i = 0; i < 32; i++) {
    pass[i] = char(EEPROM.read(EEPROM_PASS_ADDR + i));
  }
  
  Serial.print("SSID loaded: ");
  Serial.println(ssid);
  Serial.println("Password loaded: [HIDDEN]");
}

// Save WiFi credentials to EEPROM
void saveCredentials() {
  Serial.println("Saving credentials to EEPROM");
  
  // Save SSID
  for (int i = 0; i < 32; i++) {
    EEPROM.write(EEPROM_SSID_ADDR + i, ssid[i]);
  }
  
  // Save password
  for (int i = 0; i < 32; i++) {
    EEPROM.write(EEPROM_PASS_ADDR + i, pass[i]);
  }
  
  // Mark as valid
  EEPROM.write(EEPROM_VALID_ADDR, 1);
  
  EEPROM.commit();
  Serial.println("Credentials saved");
}

// Configuration Portal Functions
void startConfigPortal() {
  Serial.println("Starting configuration portal");
  
  // Set mode and stop any existing connections
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSSID, apPassword);
  
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  
  // Setup DNS server
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  // Set up web server handlers
  webServer.on("/", handleRoot);
  webServer.on("/save", handleSave);
  webServer.on("/feed", handleFeed);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  
  Serial.println("Web server started");
  
  // Set config mode and timeout
  configMode = true;
  configTimeout = CONFIG_TIMEOUT_MAX;
}

// Config Portal Web Server Handlers
void handleRoot() {
  Serial.println("Web client connected to root page");
  
  String html = "<html><head><title>Pet Feeder Setup</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; }";
  html += "input, button { margin: 10px 0; padding: 10px; width: 100%; }";
  html += "h1 { color: #2c3e50; }";
  html += "button { background-color: #3498db; color: white; border: none; cursor: pointer; }";
  html += "button:hover { background-color: #2980b9; }";
  html += ".container { max-width: 400px; margin: 0 auto; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>Pet Feeder WiFi Setup</h1>";
  html += "<form action='/save' method='POST'>";
  html += "WiFi Network Name:<br>";
  html += "<input type='text' name='ssid' value='" + String(ssid) + "'><br>";
  html += "WiFi Password:<br>";
  html += "<input type='password' name='pass' value='" + String(pass) + "'><br>";
  html += "<button type='submit'>Save & Connect</button>";
  html += "</form>";
  html += "<hr>";
  html += "<h2>Test Feeder</h2>";
  html += "<p>Press the button to test the feeding mechanism</p>";
  html += "<button onclick='window.location=\"/feed\"'>Test Feed Now</button>";
  html += "</div></body></html>";
  
  webServer.send(200, "text/html", html);
}

void handleSave() {
  Serial.println("Processing save request");
  
  if (webServer.hasArg("ssid") && webServer.hasArg("pass")) {
    String newSSID = webServer.arg("ssid");
    String newPass = webServer.arg("pass");
    
    Serial.print("New SSID: ");
    Serial.println(newSSID);
    Serial.println("New Password: [HIDDEN]");
    
    // Copy new values to our global variables
    strncpy(ssid, newSSID.c_str(), sizeof(ssid) - 1);
    strncpy(pass, newPass.c_str(), sizeof(pass) - 1);
    
    // Save to EEPROM
    saveCredentials();
    
    // Send response
    String html = "<html><head><title>Saved</title>";
    html += "<meta http-equiv='refresh' content='5;url=/' />";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body { font-family: Arial, sans-serif; margin: 20px; text-align: center; }</style>";
    html += "</head><body>";
    html += "<h1>Settings Saved!</h1>";
    html += "<p>Your settings have been saved. The device will attempt to connect...</p>";
    html += "<p>Redirecting in 5 seconds...</p>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);
    
    delay(1000);
    
    // Try to connect with new credentials
    if (tryConnect()) {
      // If successful, restart to apply
      delay(1000);
      ESP.restart();
    }
  } else {
    webServer.send(400, "text/plain", "Missing Parameters");
  }
}

void handleFeed() {
  Serial.println("Test feeding requested from web portal");
  
  // Trigger a feeding cycle
  feedNow();
  
  // Send response
  String html = "<html><head><title>Test Feed</title>";
  html += "<meta http-equiv='refresh' content='5;url=/' />";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body { font-family: Arial, sans-serif; margin: 20px; text-align: center; }</style>";
  html += "</head><body>";
  html += "<h1>Test Feed Completed</h1>";
  html += "<p>The feeder mechanism has been tested.</p>";
  html += "<p>Redirecting in 5 seconds...</p>";
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}

void handleNotFound() {
  // If in AP mode, redirect to captive portal
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

// Handle captive portal requests by redirecting to our setup page
bool captivePortal() {
  if (!isIP(webServer.hostHeader()) && webServer.hostHeader() != (String(apSSID) + ".local")) {
    Serial.print("Redirecting client to captive portal: ");
    Serial.println(webServer.hostHeader());
    
    webServer.sendHeader("Location", String("http://") + toStringIP(webServer.client().localIP()), true);
    webServer.send(302, "text/plain", "");
    webServer.client().stop();
    return true;
  }
  return false;
}

// Helper function to determine if a string is an IP address
bool isIP(String str) {
  for (uint8_t i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c != '.' && c != ':' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

// Helper function to convert IP to string
String toStringIP(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}
