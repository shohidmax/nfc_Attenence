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
#include <Preferences.h> // For saving device name

// 2. Define the pins
#define SS_PIN    5
#define RST_PIN   4
#define BUZZER_PIN 13 // Buzzer connected to GPIO 13
#define RELAY_PIN 12  // Relay connected to GPIO 12

// 3. Global Variables
String deviceId;
Preferences preferences;
WebServer server(80); // Web server on port 80

// Variables to store the last scan data for the web UI
String lastUid = "N/A";
String lastNfcData = "N/A";
String lastApiResponse = "N/A";
String lastScannedName = "N/A";
String lastScannedDesignation = "N/A";
String lastVerificationStatus = "N/A";
long lastRssi = 0;

// 4. Create instances for hardware
MFRC522 mfrc522(SS_PIN, RST_PIN);
NfcAdapter nfc = NfcAdapter(&mfrc522); 
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// 5. Time configuration for Bangladesh (UTC+6)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 6 * 3600;
const int   daylightOffset_sec = 0;

// --- Sound Helper Functions ---

void playScanSound() {
    tone(BUZZER_PIN, 1200, 100);
}

void playSuccessSound() {
    tone(BUZZER_PIN, 1500, 150);
    delay(160);
    tone(BUZZER_PIN, 1800, 150);
}

void playFailureSound() {
    tone(BUZZER_PIN, 800, 500);
}


// --- Helper Functions for Display ---

void drawWifiIcon(int rssi) {
    display.fillRect(110, 0, 18, 8, SSD1306_BLACK);
    for (int i = 0; i < 4; i++) {
        if (rssi > -80 + i * 15) {
            display.fillRect(110 + i * 4, 6 - i * 2, 3, i * 2 + 2, SSD1306_WHITE);
        }
    }
}

void showMessage(String line1, String line2 = "", int size = 1, bool clear = true) {
    if (clear) display.clearDisplay();
    display.setTextSize(size);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 5);
    display.println(line1);
    if (line2 != "") {
        display.println(line2);
    }
    display.display();
}

void displayUserInfoCard(String name, String designation) {
    display.clearDisplay();
    int cardX = 8, cardY = 4, cardW = 112, cardH = 56, cardRadius = 6;
    display.drawRoundRect(cardX, cardY, cardW, cardH, cardRadius, SSD1306_WHITE);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    int16_t x1, y1;
    uint16_t w, h;
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
<html>
<head>
    <title>ESP32 NFC Controller</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; background: #f4f4f4; color: #333; text-align: center; }
        .container { max-width: 600px; margin: auto; padding: 20px; background: #fff; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        h1, h2 { color: #0056b3; }
        .card { background: #eee; padding: 15px; margin-top: 15px; border-radius: 5px; }
        .label { font-weight: bold; }
        input[type="text"], input[type="submit"] { width: 80%; padding: 10px; margin-top: 10px; border-radius: 5px; border: 1px solid #ddd; }
        input[type="submit"] { background: #007bff; color: white; cursor: pointer; border: none; }
        .unlock-btn { background: #28a745; }
        .unlock-btn:hover { background: #218838; }
        .status-ok { color: green; }
        .status-fail { color: red; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP32 NFC Controller</h1>
        <div class="card">
            <h2>Device Status</h2>
            <p><span class="label">Device Name:</span> <span id="deviceName">--</span></p>
            <p><span class="label">WiFi Signal:</span> <span id="rssi">--</span> dBm</p>
        </div>
        <div class="card">
            <h2>Last Scan Info</h2>
            <p><span class="label">UID:</span> <span id="uid">--</span></p>
            <p><span class="label">Name:</span> <span id="name">--</span></p>
            <p><span class="label">Designation:</span> <span id="designation">--</span></p>
            <p><span class="label">Verification:</span> <span id="verification">--</span></p>
        </div>
        <div class="card">
            <h2>Manual Control</h2>
            <form action="/unlock" method="post">
                <input type="submit" class="unlock-btn" value="Unlock">
            </form>
        </div>
        <div class="card">
            <h2>Configuration</h2>
            <form action="/update" method="post">
                <label for="newId">Change Device Name:</label><br>
                <input type="text" id="newId" name="newId" placeholder="Enter new name"><br>
                <input type="submit" value="Update Name">
            </form>
        </div>
    </div>
    <script>
        function fetchData() {
            fetch('/data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('deviceName').innerText = data.deviceId;
                    document.getElementById('rssi').innerText = data.rssi;
                    document.getElementById('uid').innerText = data.lastUid;
                    document.getElementById('name').innerText = data.lastScannedName;
                    document.getElementById('designation').innerText = data.lastScannedDesignation;
                    const verifySpan = document.getElementById('verification');
                    verifySpan.innerText = data.lastVerificationStatus;
                    verifySpan.className = data.lastVerificationStatus === 'OK' ? 'status-ok' : 'status-fail';
                });
        }
        setInterval(fetchData, 2000);
        window.onload = fetchData;
    </script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", html);
}

void handleData() {
    JsonDocument doc;
    doc["deviceId"] = deviceId;
    doc["rssi"] = lastRssi;
    doc["lastUid"] = lastUid;
    doc["lastScannedName"] = lastScannedName;
    doc["lastScannedDesignation"] = lastScannedDesignation;
    doc["lastVerificationStatus"] = lastVerificationStatus;
    String jsonString;
    serializeJson(doc, jsonString);
    server.send(200, "application/json", jsonString);
}

void handleUpdate() {
    if (server.hasArg("newId")) {
        String newId = server.arg("newId");
        if (newId.length() > 0) {
            deviceId = newId;
            preferences.putString("deviceId", deviceId);
        }
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Updated!");
}

void handleUnlock() {
    Serial.println("Manual unlock request from web panel.");
    playSuccessSound();
    digitalWrite(RELAY_PIN, HIGH);
    delay(5000);
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("Relay deactivated.");
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Unlocked!");
}

void setup() {
  Serial.begin(115200);
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Ensure relay is off at start

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    for(;;);
  }

  preferences.begin("nfc-app", false);
  deviceId = preferences.getString("deviceId", "ESP32-NFC-01");
  
  showMessage("Device: " + deviceId, "Starting...");
  delay(2000);

  WiFiManager wifiManager;
  showMessage("Connecting WiFi...", "AP: ESP32-NFC-Setup");
  if (!wifiManager.autoConnect("ESP32-NFC-Setup")) {
    ESP.restart();
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/unlock", HTTP_POST, handleUnlock); // New endpoint for unlock
  server.begin();
  
  showMessage("WiFi Connected!", "IP: " + WiFi.localIP().toString());
  delay(2500);

  SPI.begin();
  mfrc522.PCD_Init();
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
  nfc.begin();
}

void loop() {
  server.handleClient();

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

  if (WiFi.status() != WL_CONNECTED) {
    ESP.restart();
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
        
        // Activate relay for 5 seconds
        Serial.println("Access Granted. Activating Relay.");
        digitalWrite(RELAY_PIN, HIGH);
        delay(5000);
        digitalWrite(RELAY_PIN, LOW);
        Serial.println("Relay Deactivated.");
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
