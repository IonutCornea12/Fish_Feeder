/*
 * ESP32 Fish Feeder
 *   - Connects to Wi-Fi
 *   - Syncs time via NTP (Romania)
 *   - Hosts a webpage for:
 *       * Manual feed
 *       * Automatic feed schedule
 *   - Displays last 5 feed events
 *   - Uses Bootstrap for nicer styling
 */

// --------------------------------------------------------------------------
// 1) Includes
// --------------------------------------------------------------------------
#include <WiFi.h>        // For Wi-Fi
#include <WebServer.h>   // For web server
#include <ESP32Servo.h>  // For servo
#include <time.h>        // For NTP synchronization time
#include <Preferences.h>

// --------------------------------------------------------------------------
// 2) Wi-Fi Credentials
// --------------------------------------------------------------------------
const char* SSID = "UTCN-Guest";
const char* PASS = "utcluj.ro";

// --------------------------------------------------------------------------
// 3) NTP/Time Config for Romania
// --------------------------------------------------------------------------
static const long gmtOffset_sec      = 7200; //(GMT+2 - Romania timezone)
static const int  daylightOffset_sec = 0;    
static const char* ntpServer         = "pool.ntp.org";

// --------------------------------------------------------------------------
// 4) Servo Setup
// --------------------------------------------------------------------------
Servo feederServo;
const int servoPin = 2; // Connect servo signal to GPIO2

// --------------------------------------------------------------------------
// 5) Automatic Feed Schedule
//    Day-of-week: 0=Sunday, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat
// --------------------------------------------------------------------------
int feedDayOfWeek = -1;  
int feedHour      = -1; 
int feedMinute    = -1; 
bool hasFedThisWeek = false; // prevents repeated feeds within the same minute

// --------------------------------------------------------------------------
// 6) Tracking Last 5 Feed Events
// --------------------------------------------------------------------------
#define FEED_EVENT_COUNT 5
String feedEvents[FEED_EVENT_COUNT]; 
int feedEventIndex = 0;

// --------------------------------------------------------------------------
// 7) WebServer
// --------------------------------------------------------------------------
WebServer server(80);//start sv on port 80(default)

// --------------------------------------------------------------------------
// 8) Forward Declarations && preferences
// --------------------------------------------------------------------------
void handleRoot();                  // serves the main page
void handleFeed();                  // triggers manual feed
void handleSetSchedule();           // sets the new schedule from query params
void handleGetState();              // returns JSON with current schedule & events
void checkFeedingSchedule();        // checks if it's time to automatically feed
void feedFish(bool manual);         // rotates servo, logs event
void setupTime();                   // configures NTP time
Preferences preferences;  // Preferences object for storing schedule
// --------------------------------------------------------------------------
// 9) Single-Page HTML/JS (with Bootstrap)
// --------------------------------------------------------------------------
String indexPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Fish Feeder</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <!-- Bootstrap 5 CSS (CDN) -->
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css">
  <style>
    body {
      background-color: #f8f9fa; /* Light gray background */
    }
    #msg {
      margin-top: 10px;
    }
  </style>
</head>
<body>

<div class="container py-4">
  <h1 class="mb-4">ESP32 Fish Feeder</h1>

  <!-- Card for Manual Feed -->
  <div class="card mb-4">
    <div class="card-body">
      <h2 class="card-title">Manual Feed</h2>
      <p>Click the button below to feed fish immediately.</p>
      <button class="btn btn-primary btn-lg" onclick="feedNow()">Feed Now</button>
      <p id="msg" class="text-success fw-bold mt-3"></p>
    </div>
  </div>

  <!-- Card for Current Schedule -->
  <div class="card mb-4">
    <div class="card-body">
      <h2 class="card-title">Current Schedule</h2>
      <p id="currentSchedule" class="fs-5 fw-bold">Loading...</p>
    </div>
  </div>

  <!-- Card for Setting Schedule -->
  <div class="card mb-4">
    <div class="card-body">
      <h2 class="card-title">Set Automatic Feeding Schedule</h2>
      <form onsubmit="setSchedule(event)" class="row g-3 mt-2">
        <div class="col-md-4">
          <label for="day" class="form-label fw-semibold">Day of Week</label>
          <select id="day" class="form-select">
            <option value="0">Monday</option>
            <option value="1">Tuesday</option>
            <option value="2">Wednesday</option>
            <option value="3">Thursday</option>
            <option value="4">Friday</option>
            <option value="5">Saturday</option>
            <option value="6">Sunday</option>
          </select>
        </div>
        <div class="col-md-4">
          <label for="hour" class="form-label fw-semibold">Hour (0-23)</label>
          <input type="number" id="hour" min="0" max="23" value="10" class="form-control"/>
        </div>
        <div class="col-md-4">
          <label for="minute" class="form-label fw-semibold">Minute (0-59)</label>
          <input type="number" id="minute" min="0" max="59" value="0" class="form-control"/>
        </div>
        <div class="col-12">
          <button type="submit" class="btn btn-success">Set Schedule</button>
        </div>
      </form>
    </div>
  </div>

  <!-- Card for Feed History -->
  <div class="card mb-4 mx-auto">
  <div class="card-body">
    <h2 class="card-title text-center">Feed History (Last 5)</h2>
    <ul id="events" class="list-group mt-3"></ul>
  </div>
</div>
</div>

<!-- JavaScript -->
<script>
  // On page load
  window.onload = () => {
    updateUI();
    // Poll every 10 seconds so scheduled feeds appear automatically
    setInterval(updateUI, 10000);
  };

  function updateUI() {
    fetch('/getstate')
      .then(res => res.json())
          .then(data => {
          // data.feedDay, data.feedHour, data.feedMinute, data.events
          let dayNames = ["Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"];

          if (data.feedDay < 0 || data.feedHour < 0 || data.feedMinute < 0) {
            // Show "no schedule"
            document.getElementById("currentSchedule").innerText = "No schedule set yet";
          } else {
            let schedStr = "Day: " + dayNames[data.feedDay] +
                          ", Time: " + data.feedHour + ":" +
                          (data.feedMinute < 10 ? '0'+data.feedMinute : data.feedMinute);
            document.getElementById("currentSchedule").innerText = schedStr;
          }

        // Feed events
        const eventsList = document.getElementById("events");
        eventsList.innerHTML = "";
        data.events.forEach(e => {
          let li = document.createElement("li");
          li.className = "list-group-item";
          li.textContent = e;
          eventsList.appendChild(li);
        });
      })
      .catch(err => console.error(err));
  }

  function feedNow() {
    fetch('/feed')
      .then(res => res.text())
      .then(txt => {
        document.getElementById("msg").innerText = txt;
        updateUI();
      })
      .catch(err => console.error(err));
  }

  function setSchedule(ev) {
    ev.preventDefault();
    const day    = document.getElementById("day").value;
    const hour   = document.getElementById("hour").value;
    const minute = document.getElementById("minute").value;

    const url = `/setschedule?day=${day}&hour=${hour}&minute=${minute}`;
    fetch(url)
      .then(res => res.text())
      .then(txt => {
        document.getElementById("msg").innerText = txt;
        updateUI();
      })
      .catch(err => console.error(err));
  }
</script>

<!-- Bootstrap 5 JS (CDN) -->
<script src="https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js"></script>
</body>
</html>
)rawliteral";

// --------------------------------------------------------------------------
// 10) setupTime(): Configure time via NTP
// --------------------------------------------------------------------------
void setupTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.print("[Time] Waiting for NTP sync");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println(" done!");

  // Print for debug
  char buf[64];
  strftime(buf, sizeof(buf), "%c", &timeinfo);
  Serial.print("[Time] Local time: ");
  Serial.println(buf);
}

// --------------------------------------------------------------------------
// 11) Arduino setup()
// --------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[Setup] Starting...");
// Open NVS storage
  preferences.begin("fish-feeder", true);  // Read-only mode
  feedDayOfWeek = preferences.getInt("feedDayOfWeek", -1); 
  feedHour = preferences.getInt("feedHour", -1);
  feedMinute = preferences.getInt("feedMinute", -1);
  preferences.end(); 

  Serial.printf("[Schedule] Retrieved schedule: Day: %d, Hour: %d, Minute: %d\n", feedDayOfWeek, feedHour, feedMinute);

  // Connect to Wi-Fi
  Serial.printf("[WiFi] Connecting to %s\n", SSID);
  WiFi.begin(SSID, PASS);

  int tries = 30;
  while (WiFi.status() != WL_CONNECTED && tries > 0) {
    delay(500);
    Serial.print(".");
    tries--;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connected!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WiFi] Failed to connect.");
  }

  // NTP Time
  setupTime();

  // Servo
  feederServo.attach(servoPin, 500, 2400);//µs

  // Register WebServer routes
  server.on("/", handleRoot);
  server.on("/feed", handleFeed);
  server.on("/setschedule", handleSetSchedule);
  server.on("/getstate", handleGetState);

  server.begin();
  Serial.println("[WebServer] Listening on port 80");
}

// --------------------------------------------------------------------------
// 12) loop()
// --------------------------------------------------------------------------
void loop() {
  server.handleClient();
  checkFeedingSchedule();
}

// --------------------------------------------------------------------------
// 13) handleRoot(): Serve the main page
// --------------------------------------------------------------------------
void handleRoot() {
  server.send(200, "text/html", indexPage);
}

// --------------------------------------------------------------------------
// 14) handleFeed(): Manual feed
// --------------------------------------------------------------------------
void handleFeed() {
  feedFish(/*manual=*/true);
  server.send(200, "text/plain", "Manual feed performed!");
}

// --------------------------------------------------------------------------
// 15) handleSetSchedule(): Receives day/hour/minute in query
// --------------------------------------------------------------------------
void handleSetSchedule() {
  if (server.hasArg("day")) {
    feedDayOfWeek = server.arg("day").toInt();   // 0..6
  }
  if (server.hasArg("hour")) {
    feedHour = server.arg("hour").toInt();       // 0..23
  }
  if (server.hasArg("minute")) {
    feedMinute = server.arg("minute").toInt();   // 0..59
  }
  hasFedThisWeek = false;
  preferences.begin("fish-feeder", false);  // Read-write mode (false)
  preferences.putInt("feedDayOfWeek", feedDayOfWeek);
  preferences.putInt("feedHour", feedHour);
  preferences.putInt("feedMinute", feedMinute);
  preferences.end(); 

  Serial.println("[Schedule] Schedule saved to NVS.");
  const char* dayNames[7] = {
    "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday", "Sunday"
  };

  // Build the response
  String msg;
  if (feedDayOfWeek >= 0 && feedHour >= 0 && feedMinute >= 0) {
    msg = "Schedule set to " + String(dayNames[feedDayOfWeek]) 
        + ", " + String(feedHour) + ":" + String(feedMinute);
  } else {
    msg = "No valid schedule set.";
  }

  Serial.println("[Schedule] " + msg);
  server.send(200, "text/plain", msg);
}

// --------------------------------------------------------------------------
// 16) handleGetState(): Return schedule & feed events as JSON
// --------------------------------------------------------------------------
void handleGetState() {
  // Build JSON:
  // {
  //   "feedDay": 5,
  //   "feedHour": 10,
  //   "feedMinute": 0,
  //   "events": ["Manual feed at 2025-01-02 12:00:00", ...]
  // }
  String json = "{";
  json += "\"feedDay\":" + String(feedDayOfWeek) + ",";
  json += "\"feedHour\":" + String(feedHour) + ",";
  json += "\"feedMinute\":" + String(feedMinute) + ",";
  json += "\"events\":[";

  for (int i = 0; i < FEED_EVENT_COUNT; i++) {
    int idx = (feedEventIndex + i) % FEED_EVENT_COUNT;
    String e = feedEvents[idx];
    json += "\"" + e + "\"";
    if (i < FEED_EVENT_COUNT - 1) json += ",";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

// --------------------------------------------------------------------------
// 17) checkFeedingSchedule(): Automatic feed if day/time matches
// --------------------------------------------------------------------------
void checkFeedingSchedule() {
  // If no schedule is set yet, do nothing
  if (feedDayOfWeek < 0 || feedHour < 0 || feedMinute < 0) {
    return;  
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return;
  }

  int currentDay    = (timeinfo.tm_wday + 6) % 7;  //  0=Mon, ...6=Sunday,
  // +6) % 7 to have instead of sunday = 0, monday = 1
  int currentHour   = timeinfo.tm_hour;
  int currentMinute = timeinfo.tm_min;

  if ( currentDay == feedDayOfWeek &&
       currentHour == feedHour &&
       currentMinute == feedMinute )
  {
    if (!hasFedThisWeek) {
      Serial.println("[Scheduler] Automatic feed triggered!");
      feedFish(false);
      hasFedThisWeek = true;
    }
  } else {
    // If the day changed, allow feeding again next time
    if (currentDay != feedDayOfWeek) {
      hasFedThisWeek = false;
    }
  }
}

// --------------------------------------------------------------------------
// 18) feedFish(): Actually rotate servo & log feed event
// --------------------------------------------------------------------------
void feedFish(bool manual) {
  feederServo.write(180);
  delay(1000);
  feederServo.write(0);

  // Add a feed event
  const char* feedType = manual ? "Manual" : "Scheduled";

  // Get local time
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    feedEvents[feedEventIndex] = String(feedType) + " feed at unknown time";
  } else {
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    feedEvents[feedEventIndex] = String(feedType) + " feed at " + timeStr;
  }
  feedEventIndex = (feedEventIndex + 1) % FEED_EVENT_COUNT;

  Serial.println("[Servo] Feed action performed (" + String(feedType) + ").");
}