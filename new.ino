// 1. Include the necessary libraries
#include <SPI.h>
#include <MFRC522.h>
#include <NfcAdapter.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Preferences.h>

// 2. Define the pins
#define SS_PIN    5
#define RST_PIN   4
#define BUZZER_PIN 13
#define RELAY_PIN 12

// 3. Global Variables
String deviceId;
unsigned int relayDelay;
Preferences preferences;
WebServer server(80);

// -- New Variables for Status & Backup --
unsigned long lastStatusCheck = 0;
const long statusCheckInterval = 300000; // 5 minutes
String serverStatus = "Unknown";
bool backupModeEnabled = false;
int backupReadIndex = 0;
int backupWriteIndex = 0;
int backupCount = 0;
const int MAX_BACKUP_SCANS = 100;

// Variables for web UI
String lastUid = "N/A", lastNfcData = "N/A", lastApiResponse = "N/A";
String lastScannedName = "N/A", lastScannedDesignation = "N/A", lastVerificationStatus = "N/A";
long lastRssi = 0;

// 4. Hardware Instances
MFRC522 mfrc522(SS_PIN, RST_PIN);
NfcAdapter nfc = NfcAdapter(&mfrc522); 
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// 5. Time configuration
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 6 * 3600;
const int   daylightOffset_sec = 0;

// --- Forward Declarations ---
void sendBackedUpData();
void checkServerStatus();

// --- Sound & Display Helpers (unchanged) ---
void playScanSound() { tone(BUZZER_PIN, 1200, 100); }
void playSuccessSound() { tone(BUZZER_PIN, 1500, 150); delay(160); tone(BUZZER_PIN, 1800, 150); }
void playFailureSound() { tone(BUZZER_PIN, 800, 500); }
void drawWifiIcon(int rssi) {
    display.fillRect(110, 0, 18, 8, SSD1306_BLACK);
    for (int i = 0; i < 4; i++) {
        if (rssi > -80 + i * 15) display.fillRect(110 + i * 4, 6 - i * 2, 3, i * 2 + 2, SSD1306_WHITE);
    }
}
void showMessage(String line1, String line2 = "", int size = 1, bool clear = true) {
    if (clear) display.clearDisplay();
    display.setTextSize(size);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 5);
    display.println(line1);
    if (line2 != "") display.println(line2);
    display.display();
}
void displayUserInfoCard(String name, String designation) {
    display.clearDisplay();
    int cardX = 8, cardY = 4, cardW = 112, cardH = 56, cardRadius = 6;
    display.drawRoundRect(cardX, cardY, cardW, cardH, cardRadius, SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, 18);
    display.println(name);
    display.drawLine(cardX + 10, 32, cardX + cardW - 10, 32, SSD1306_WHITE);
    display.getTextBounds(designation, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, 40);
    display.println(designation);
    display.display();
}

// --- Web Server Handlers ---
void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html data-theme="light">
<head>
    <meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NFC Controller</title>
    <link href="https://cdn.jsdelivr.net/npm/daisyui@4.11.1/dist/full.min.css" rel="stylesheet" type="text/css" />
    <script src="https://cdn.tailwindcss.com"></script>
</head>
<body class="bg-base-200 min-h-screen p-4">
    <div class="max-w-xl mx-auto">
        <div class="navbar bg-base-100 rounded-box shadow-lg">
            <div class="flex-1"><a class="btn btn-ghost text-xl">NFC Access Control</a></div>
            <div class="flex-none">
                <button class="btn btn-square btn-ghost" onclick="settings_modal.showModal()">
                    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" class="inline-block w-5 h-5 stroke-current"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 12h.01M12 12h.01M19 12h.01M6 12a1 1 0 11-2 0 1 1 0 012 0zm7 0a1 1 0 11-2 0 1 1 0 012 0zm7 0a1 1 0 11-2 0 1 1 0 012 0z"></path></svg>
                </button>
            </div>
        </div>

        <div class="grid md:grid-cols-2 gap-4 mt-4">
            <div class="card bg-base-100 shadow-xl">
                <div class="card-body">
                    <h2 class="card-title">Device Status</h2>
                    <p><span class="font-bold">Name:</span> <span id="deviceName">--</span></p>
                    <div class="flex items-center"><span class="font-bold mr-2">WiFi:</span><div id="wifi-icon-svg" class="w-6 h-6"></div><span id="rssi" class="ml-1">--</span> dBm</div>
                    <p><span class="font-bold">Server:</span> <span id="serverStatus" class="badge badge-ghost">--</span></p>
                </div>
            </div>
            <div class="card bg-base-100 shadow-xl">
                <div class="card-body">
                    <h2 class="card-title">Backup Status</h2>
                    <p><span class="font-bold">Mode:</span> <span id="backupMode">--</span></p>
                    <p><span class="font-bold">Saved Scans:</span> <span id="backupCount">--</span></p>
                    <div class="card-actions justify-end">
                        <form action="/toggle-backup" method="post"><button class="btn btn-sm btn-warning">Toggle Mode</button></form>
                    </div>
                </div>
            </div>
        </div>

        <div class="card bg-base-100 shadow-xl mt-4">
            <div class="card-body">
                <h2 class="card-title">Last Scan Information</h2>
                <div class="overflow-x-auto">
                    <table class="table"><tbody>
                        <tr><th>UID</th><td id="uid">N/A</td></tr>
                        <tr><th>Name</th><td id="name">N/A</td></tr>
                        <tr><th>Designation</th><td id="designation">N/A</td></tr>
                        <tr><th>Verification</th><td><span id="verification" class="badge badge-ghost">N/A</span></td></tr>
                    </tbody></table>
                </div>
            </div>
        </div>
        
        <div class="card bg-base-100 shadow-xl mt-4">
             <div class="card-body">
                <h2 class="card-title">Manual Control</h2>
                <div class="card-actions justify-end">
                    <form action="/unlock" method="post"><button class="btn btn-success">Unlock Door</button></form>
                </div>
            </div>
        </div>

        <dialog id="settings_modal" class="modal">
            <div class="modal-box">
                <h3 class="font-bold text-lg">Settings</h3>
                <form method="post" action="/settings">
                    <div class="form-control w-full mt-4"><label class="label"><span class="label-text">Device Name</span></label><input type="text" name="newId" id="modalDeviceName" class="input input-bordered w-full" /></div>
                    <div class="form-control w-full mt-2"><label class="label"><span class="label-text">Relay Unlock Time (ms)</span></label><input type="number" name="delay" id="modalRelayDelay" class="input input-bordered w-full" /></div>
                    <div class="modal-action"><button type="submit" class="btn btn-primary">Save</button><button type="button" class="btn" onclick="settings_modal.close()">Close</button></div>
                </form>
                <form method="post" action="/reset-wifi" class="mt-4"><button class="btn btn-error w-full">Reset WiFi Settings</button></form>
            </div>
            <form method="dialog" class="modal-backdrop"><button>close</button></form>
        </dialog>
    </div>
    <script>
        function updateWifiIcon(rssi){let bars=0;if(rssi>-55)bars=4;else if(rssi>-65)bars=3;else if(rssi>-75)bars=2;else if(rssi>-85)bars=1;const colors=['#d1d5db','#d1d5db','#d1d5db','#d1d5db'];for(let i=0;i<bars;i++){colors[i]='#10b981';}document.getElementById('wifi-icon-svg').innerHTML=`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.55a11 11 0 0 1 14.08 0" stroke="${colors[3]}"></path><path d="M8.5 16.05a6 6 0 0 1 6.98 0" stroke="${colors[2]}"></path><path d="M12 19.5a2 2 0 0 1 .02 0" stroke="${colors[1]}"></path><path d="M12 19.5a2 2 0 0 1 .02 0" fill="${colors[0]}" stroke="none"></path></svg>`;}
        function fetchData(){fetch('/data').then(r=>r.json()).then(d=>{document.getElementById('deviceName').innerText=d.deviceId;document.getElementById('modalDeviceName').value=d.deviceId;document.getElementById('rssi').innerText=d.rssi;document.getElementById('uid').innerText=d.lastUid;document.getElementById('name').innerText=d.lastScannedName;document.getElementById('designation').innerText=d.lastScannedDesignation;document.getElementById('modalRelayDelay').value=d.relayDelay;const v=document.getElementById('verification');v.innerText=d.lastVerificationStatus;if(d.lastVerificationStatus==='OK')v.className='badge badge-success';else if(d.lastVerificationStatus==='N/A')v.className='badge badge-ghost';else v.className='badge badge-error';const s=document.getElementById('serverStatus');s.innerText=d.serverStatus;s.className=d.serverStatus==='Active'?'badge badge-success':'badge badge-error';const b=document.getElementById('backupMode');b.innerText=d.backupModeEnabled?'ON':'OFF';b.className=d.backupModeEnabled?'badge badge-warning':'badge badge-info';document.getElementById('backupCount').innerText=d.backupCount;updateWifiIcon(d.rssi);});}
        setInterval(fetchData,2000);window.onload=fetchData;
    </script>
</body></html>
)rawliteral";
    server.send(200, "text/html", html);
}
void handleData() {
    JsonDocument doc;
    doc["deviceId"] = deviceId;
    doc["rssi"] = lastRssi;
    doc["relayDelay"] = relayDelay;
    doc["serverStatus"] = serverStatus;
    doc["backupModeEnabled"] = backupModeEnabled;
    doc["backupCount"] = backupCount;
    doc["lastUid"] = lastUid;
    doc["lastScannedName"] = lastScannedName;
    doc["lastScannedDesignation"] = lastScannedDesignation;
    doc["lastVerificationStatus"] = lastVerificationStatus;
    String jsonString;
    serializeJson(doc, jsonString);
    server.send(200, "application/json", jsonString);
}
void handleSettings() {
    if (server.hasArg("newId") && server.arg("newId").length() > 0) {
        deviceId = server.arg("newId");
        preferences.putString("deviceId", deviceId);
    }
    if (server.hasArg("delay")) {
        relayDelay = server.arg("delay").toInt();
        preferences.putUInt("relayDelay", relayDelay);
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Saved!");
}
void handleUnlock() {
    playSuccessSound();
    digitalWrite(RELAY_PIN, HIGH);
    delay(relayDelay);
    digitalWrite(RELAY_PIN, LOW);
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Unlocked!");
}
void handleResetWifi() {
    WiFiManager wm;
    wm.resetSettings();
    showMessage("WiFi Reset!", "Restarting...");
    delay(2000);
    ESP.restart();
}
void handleToggleBackup() {
    backupModeEnabled = !backupModeEnabled;
    preferences.putBool("backupMode", backupModeEnabled);
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Toggled!");
}

// --- Main Setup ---
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }

  preferences.begin("nfc-app", false);
  deviceId = preferences.getString("deviceId", "ESP32-NFC-01");
  relayDelay = preferences.getUInt("relayDelay", 5000);
  backupModeEnabled = preferences.getBool("backupMode", false);
  backupReadIndex = preferences.getInt("bkReadIdx", 0);
  backupWriteIndex = preferences.getInt("bkWriteIdx", 0);
  backupCount = preferences.getInt("bkCount", 0);
  
  showMessage("Device: " + deviceId, "Starting...");
  delay(2000);

  WiFiManager wifiManager;
  showMessage("Connecting WiFi...", "AP: ESP32-NFC-Setup");
  if (!wifiManager.autoConnect("ESP32-NFC-Setup")) ESP.restart();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/settings", HTTP_POST, handleSettings);
  server.on("/unlock", HTTP_POST, handleUnlock);
  server.on("/reset-wifi", HTTP_POST, handleResetWifi);
  server.on("/toggle-backup", HTTP_POST, handleToggleBackup);
  server.begin();
  
  showMessage("WiFi Connected!", "IP: " + WiFi.localIP().toString());
  delay(2500);

  SPI.begin();
  mfrc522.PCD_Init();
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
  nfc.begin();
}

// --- Main Loop ---
void loop() {
  server.handleClient();

  if (millis() - lastStatusCheck > statusCheckInterval) {
      lastStatusCheck = millis();
      checkServerStatus();
  }

  if (WiFi.status() == WL_CONNECTED && !backupModeEnabled && backupCount > 0) {
      sendBackedUpData();
  }

  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 1000) {
      lastDisplayUpdate = millis();
      lastRssi = WiFi.RSSI();
      display.clearDisplay();
      display.setCursor(0, 5);
      display.setTextSize(1);
      display.println("Scan an NFC tag...");
      drawWifiIcon(lastRssi);
      display.display();
  }

  if (nfc.tagPresent()) {
    playScanSound();
    NfcTag tag = nfc.read();
    lastUid = tag.getUidString();
    
    if (tag.hasNdefMessage()) {
      NdefMessage message = tag.getNdefMessage();
      NdefRecord record = message.getRecord(0);
      int payloadLength = record.getPayloadLength();
      const byte* payload = record.getPayload();
      String data = "";
      for (int i = 3; i < payloadLength; i++) { data += (char)payload[i]; }
      lastNfcData = data;
    } else {
      lastNfcData = "No NDEF data";
    }
    
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      showMessage("Time Sync Failed!");
      delay(2000);
      return;
    }
    
    char dateString[11]; strftime(dateString, sizeof(dateString), "%Y-%m-%d", &timeinfo);
    char timeString[9]; strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);

    JsonDocument doc;
    doc["deviceId"] = deviceId;
    doc["uid"] = lastUid;
    doc["data"] = lastNfcData;
    doc["date"] = dateString;
    doc["time"] = timeString;
    String jsonString;
    serializeJson(doc, jsonString);

    if (WiFi.status() != WL_CONNECTED || backupModeEnabled) {
        // --- Save to Backup ---
        if (backupCount < MAX_BACKUP_SCANS) {
            String key = "scan_" + String(backupWriteIndex);
            preferences.putString(key.c_str(), jsonString);
            backupWriteIndex = (backupWriteIndex + 1) % MAX_BACKUP_SCANS;
            backupCount++;
            preferences.putInt("bkWriteIdx", backupWriteIndex);
            preferences.putInt("bkCount", backupCount);
            showMessage("Saved Locally", String(backupCount) + " scans stored");
            playSuccessSound();
            delay(2000);
        } else {
            showMessage("Backup Full!", "Cannot save scan");
            playFailureSound();
            delay(2000);
        }
    } else {
        // --- Send to Server ---
        showMessage("Sending data...");
        HTTPClient http;
        http.begin("https://nfc-attenence.onrender.com/api/nfc");
        http.addHeader("Content-Type", "application/json");
        int httpResponseCode = http.POST(jsonString);
        
        if (httpResponseCode > 0) {
          lastApiResponse = http.getString();
          JsonDocument responseDoc;
          deserializeJson(responseDoc, lastApiResponse);
          lastScannedName = responseDoc["name"].as<String>();
          lastScannedDesignation = responseDoc["designation"].as<String>();
          lastVerificationStatus = responseDoc["verify"].as<String>();

          if (lastVerificationStatus == "OK") {
            playSuccessSound();
            displayUserInfoCard(lastScannedName, lastScannedDesignation);
            digitalWrite(RELAY_PIN, HIGH);
            delay(relayDelay);
            digitalWrite(RELAY_PIN, LOW);
          } else {
            playFailureSound();
            showMessage("Access Denied", "", 2);
            delay(4000);
          }
        } else {
          playFailureSound();
          lastApiResponse = "HTTP Post Failed";
          lastScannedName = "Error";
          lastScannedDesignation = "Post Failed";
          lastVerificationStatus = "Failed";
          showMessage(lastScannedName, lastScannedDesignation, 2);
          delay(4000);
        }
        http.end();
    }
  }
}

// --- New Functions for Status and Backup ---

void checkServerStatus() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin("https://nfc-attenence.onrender.com/api/stts");
        int httpCode = http.GET();
        if (httpCode > 0) {
            String payload = http.getString();
            JsonDocument doc;
            deserializeJson(doc, payload);
            if (doc["status"] == "active") {
                serverStatus = "Active";
            } else {
                serverStatus = "Inactive";
            }
        } else {
            serverStatus = "Error";
        }
        http.end();
    } else {
        serverStatus = "No Network";
    }
    Serial.print("Server Status: ");
    Serial.println(serverStatus);
}

void sendBackedUpData() {
    if (backupCount > 0) {
        String key = "scan_" + String(backupReadIndex);
        String jsonData = preferences.getString(key.c_str(), "");
        
        if (jsonData.length() > 0) {
            Serial.println("Sending backed up scan...");
            showMessage("Syncing...", String(backupCount) + " left");

            HTTPClient http;
            http.begin("https://nfc-attenence.onrender.com/api/nfc");
            http.addHeader("Content-Type", "application/json");
            int httpResponseCode = http.POST(jsonData);

            if (httpResponseCode > 0) {
                // Success, remove from backup
                preferences.remove(key.c_str());
                backupReadIndex = (backupReadIndex + 1) % MAX_BACKUP_SCANS;
                backupCount--;
                preferences.putInt("bkReadIdx", backupReadIndex);
                preferences.putInt("bkCount", backupCount);
                Serial.println("Backup scan sent successfully.");
            } else {
                Serial.println("Failed to send backup scan, will retry.");
            }
            http.end();
        }
    }
}
