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

// -- Security & Session Variables --
String adminPassword;
unsigned long sessionStartTime = 0;
const long sessionTimeout = 600000; // 10 minutes

// -- API URL Variables --
String apiUrlNfc;
String apiUrlStatus;
String apiUrlUpdate;

// -- Variables for Status & Backup --
unsigned long lastStatusCheck = 0;
const long statusCheckInterval = 300000; // 5 minutes
String serverStatus = "Unknown";
bool backupModeEnabled = false;
bool manualUpdateRequested = false;
int backupReadIndex = 0, backupWriteIndex = 0, backupCount = 0;
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
bool isAuthenticated();

// --- Sound & Display Helpers ---
void playScanSound() { tone(BUZZER_PIN, 1200, 100); }
void playSuccessSound() { tone(BUZZER_PIN, 1500, 150); delay(160); tone(BUZZER_PIN, 1800, 150); }
void playFailureSound() { tone(BUZZER_PIN, 800, 500); }
void drawWifiIcon(int rssi) { for (int i = 0; i < 4; i++) { if (rssi > -80 + i * 15) display.fillRect(110 + i * 4, 6 - i * 2, 3, i * 2 + 2, SSD1306_WHITE); } }
void showMessage(String l1, String l2="", int s=1, bool c=true) { if(c)display.clearDisplay(); display.setTextSize(s); display.setTextColor(SSD1306_WHITE); display.setCursor(0,5); display.println(l1); if(l2!="")display.println(l2); display.display(); }
void displayUserInfoCard(String name, String designation) { display.clearDisplay(); int cX=8,cY=4,cW=112,cH=56,cR=6; display.drawRoundRect(cX,cY,cW,cH,cR,SSD1306_WHITE); display.setTextSize(1); display.setTextColor(SSD1306_WHITE); int16_t x,y; uint16_t w,h; display.getTextBounds(name,0,0,&x,&y,&w,&h); display.setCursor((128-w)/2,18); display.println(name); display.drawLine(cX+10,32,cX+cW-10,32,SSD1306_WHITE); display.getTextBounds(designation,0,0,&x,&y,&w,&h); display.setCursor((128-w)/2,40); display.println(designation); display.display(); }

// --- Animation Functions ---
void animateSending() { for (int i=0;i<4;i++) { display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(28,28); String t="Sending"; for(int j=0;j<i;j++)t+="."; display.print(t); display.display(); delay(250); } }
void animateSuccess() { display.clearDisplay(); for(int i=0;i<360;i+=20){float a=i*0.0174533; display.drawPixel(64+20*cos(a),32+20*sin(a),SSD1306_WHITE); display.display();} display.drawCircle(64,32,20,SSD1306_WHITE); display.drawLine(54,32,62,40,SSD1306_WHITE); display.drawLine(62,40,78,24,SSD1306_WHITE); display.display(); delay(1000); }
void animateFailure() { display.clearDisplay(); display.drawCircle(64,32,20,SSD1306_WHITE); display.display(); delay(200); display.drawLine(53,21,75,43,SSD1306_WHITE); display.display(); delay(200); display.drawLine(75,21,53,43,SSD1306_WHITE); display.display(); delay(1500); }
void animateSaving() { display.clearDisplay(); display.drawRoundRect(48,20,32,24,4,SSD1306_WHITE); display.fillRect(52,20,24,12,SSD1306_WHITE); display.fillRect(60,32,8,10,SSD1306_WHITE); display.display(); delay(400); for(int y=5;y<18;y+=4){display.fillRect(50,22,28,20,SSD1306_BLACK); display.drawLine(64,y,64,y+8,SSD1306_WHITE); display.drawLine(64,y+8,60,y+4,SSD1306_WHITE); display.drawLine(64,y+8,68,y+4,SSD1306_WHITE); display.display(); delay(150);} }

// --- Web Server Handlers ---
String getPageWrapper(String title, String content) {
    String html = "<!DOCTYPE html><html data-theme='light'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>";
    html += title;
    html += "</title><link href='https://cdn.jsdelivr.net/npm/daisyui@4.11.1/dist/full.min.css' rel='stylesheet' type='text/css' /><script src='https://cdn.tailwindcss.com'></script></head><body class='bg-base-200 min-h-screen p-4 flex items-center justify-center'><div class='max-w-md w-full'>";
    html += content;
    html += "</div></body></html>";
    return html;
}

void handleLogin() {
    String content = R"rawliteral(
        <div class="card bg-base-100 shadow-xl">
            <div class="card-body">
                <h2 class="card-title">Admin Login</h2>
                <form method="post" action="/login">
                    <div class="form-control w-full mt-4">
                        <label class="label"><span class="label-text">Password</span></label>
                        <input type="password" name="password" placeholder="Enter password" class="input input-bordered w-full" required />
                    </div>
                    <div class="form-control mt-6">
                        <button type="submit" class="btn btn-primary">Login</button>
                    </div>
                </form>
                <div class="text-center mt-4"><a href="/forgot-password" class="link link-secondary">Forgot Password?</a></div>
            </div>
        </div>
    )rawliteral";
    server.send(200, "text/html", getPageWrapper("Login", content));
}

void handleDoLogin() {
    if (server.hasArg("password") && server.arg("password") == adminPassword) {
        sessionStartTime = millis();
        server.sendHeader("Location", "/");
        server.send(302);
    } else {
        String content = R"rawliteral(
            <div class="card bg-base-100 shadow-xl">
                <div class="card-body items-center text-center">
                    <h2 class="card-title text-error">Login Failed!</h2>
                    <p>Incorrect password. Please try again.</p>
                    <div class="card-actions justify-end mt-4">
                        <a href="/login" class="btn btn-primary">Try Again</a>
                    </div>
                </div>
            </div>
        )rawliteral";
        server.send(401, "text/html", getPageWrapper("Login Failed", content));
    }
}

void handleForgotPassword() {
    String content = R"rawliteral(
        <div class="card bg-base-100 shadow-xl">
            <div class="card-body">
                <h2 class="card-title">Reset Password</h2>
                <p>To reset your password, please enter the device's name.</p>
                <form method="post" action="/forgot-password">
                    <div class="form-control w-full mt-4">
                        <label class="label"><span class="label-text">Device Name</span></label>
                        <input type="text" name="deviceName" placeholder="Enter device name" class="input input-bordered w-full" required />
                    </div>
                    <div class="form-control w-full mt-2">
                        <label class="label"><span class="label-text">New Password</span></label>
                        <input type="password" name="newPassword" placeholder="Enter new password" class="input input-bordered w-full" required />
                    </div>
                    <div class="form-control mt-6">
                        <button type="submit" class="btn btn-primary">Reset Password</button>
                    </div>
                </form>
                 <div class="text-center mt-4"><a href="/login" class="link">Back to Login</a></div>
            </div>
        </div>
    )rawliteral";
    server.send(200, "text/html", getPageWrapper("Reset Password", content));
}

void handleDoReset() {
    if (server.hasArg("deviceName") && server.arg("deviceName") == deviceId && server.hasArg("newPassword")) {
        adminPassword = server.arg("newPassword");
        preferences.putString("adminPass", adminPassword);
        String content = R"rawliteral(
            <div class="card bg-base-100 shadow-xl">
                <div class="card-body items-center text-center">
                    <h2 class="card-title text-success">Password Reset!</h2>
                    <p>Your password has been successfully updated.</p>
                    <div class="card-actions justify-end mt-4">
                        <a href="/login" class="btn btn-primary">Login Now</a>
                    </div>
                </div>
            </div>
        )rawliteral";
        server.send(200, "text/html", getPageWrapper("Success", content));
    } else {
        server.send(400, "text/plain", "Incorrect device name or missing new password.");
    }
}


void handleRoot() {
    if (!isAuthenticated()) { server.sendHeader("Location", "/login"); server.send(302); return; }
    String html = R"rawliteral(
<!DOCTYPE html>
<html data-theme="light">
<head>
    <meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>NFC Controller</title>
    <link href="https://cdn.jsdelivr.net/npm/daisyui@4.11.1/dist/full.min.css" rel="stylesheet" type="text/css" />
    <script src="https://cdn.tailwindcss.com"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/jspdf/2.5.1/jspdf.umd.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/jspdf-autotable/3.8.2/jspdf.plugin.autotable.min.js"></script>
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
                    <div class="flex items-center"><span class="font-bold mr-2">Server:</span><span id="serverStatus" class="badge badge-ghost">--</span>
                        <button class="btn btn-xs btn-ghost btn-square ml-1" onclick="checkServerNow()">
                           <svg xmlns="http://www.w3.org/2000/svg" class="h-4 w-4" fill="none" viewBox="0 0 24 24" stroke="currentColor"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17.65 6.35C16.2 4.9 14.21 4 12 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08c-.82 2.33-3.04 4-5.65 4-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z"></path></svg>
                        </button>
                    </div>
                </div>
            </div>
            <div class="card bg-base-100 shadow-xl">
                <div class="card-body">
                    <h2 class="card-title">Backup Status</h2>
                    <div class="form-control">
                        <label class="label cursor-pointer">
                            <span class="label-text font-bold">Backup Mode</span> 
                            <input type="checkbox" class="toggle toggle-warning" id="backupToggle" onchange="toggleBackupMode()" />
                        </label>
                    </div>
                    <p><span class="font-bold">Saved Scans:</span> <span id="backupCount">--</span></p>
                    <div class="card-actions justify-end space-x-2">
                        <form action="/update-all" method="post"><button class="btn btn-sm btn-info">Update</button></form>
                        <button onclick="downloadPDF()" class="btn btn-sm btn-secondary">Download PDF</button>
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
                        <tr><th>Card Data</th><td id="cardData">N/A</td></tr>
                        <tr><th>Name (from Server)</th><td id="name">N/A</td></tr>
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
                <form method="post" id="settingsForm">
                    <div class="form-control w-full mt-4"><label class="label"><span class="label-text">Device Name</span></label><input type="text" name="newId" id="modalDeviceName" class="input input-bordered w-full" /></div>
                    <div class="form-control w-full mt-2"><label class="label"><span class="label-text">Relay Unlock Time (ms)</span></label><input type="number" name="delay" id="modalRelayDelay" class="input input-bordered w-full" /></div>
                    <div class="divider">API Endpoints</div>
                    <div class="form-control w-full"><label class="label"><span class="label-text">NFC Scan API</span></label><input type="text" name="apiUrlNfc" id="modalApiUrlNfc" class="input input-bordered w-full" /></div>
                    <div class="form-control w-full mt-2"><label class="label"><span class="label-text">Status Check API</span></label><input type="text" name="apiUrlStatus" id="modalApiUrlStatus" class="input input-bordered w-full" /></div>
                    <div class="form-control w-full mt-2"><label class="label"><span class="label-text">Backup Update API</span></label><input type="text" name="apiUrlUpdate" id="modalApiUrlUpdate" class="input input-bordered w-full" /></div>
                    <div class="divider">Change Password</div>
                    <div class="form-control w-full"><label class="label"><span class="label-text">Old Password</span></label><input type="password" name="oldPassword" placeholder="Current password" class="input input-bordered w-full" /></div>
                    <div class="form-control w-full mt-2"><label class="label"><span class="label-text">New Password</span></label><input type="password" name="newPassword" placeholder="New password (optional)" class="input input-bordered w-full" /></div>
                    <div class="modal-action"><button type="button" class="btn btn-primary" onclick="saveSettings()">Save</button><button type="button" class="btn" onclick="settings_modal.close()">Close</button></div>
                </form>
                <div class="divider"></div>
                <form method="post" action="/reset-wifi" class="mt-2"><button class="btn btn-error w-full">Reset WiFi Settings</button></form>
                <form method="post" action="/restart" class="mt-2"><button class="btn btn-accent w-full">Restart Device</button></form>
            </div>
            <form method="dialog" class="modal-backdrop"><button>close</button></form>
        </dialog>
    </div>
    <script>
        function updateWifiIcon(rssi){let bars=0;if(rssi>-55)bars=4;else if(rssi>-65)bars=3;else if(rssi>-75)bars=2;else if(rssi>-85)bars=1;const colors=['#d1d5db','#d1d5db','#d1d5db','#d1d5db'];for(let i=0;i<bars;i++){colors[i]='#10b981';}document.getElementById('wifi-icon-svg').innerHTML=`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.55a11 11 0 0 1 14.08 0" stroke="${colors[3]}"></path><path d="M8.5 16.05a6 6 0 0 1 6.98 0" stroke="${colors[2]}"></path><path d="M12 19.5a2 2 0 0 1 .02 0" stroke="${colors[1]}"></path><path d="M12 19.5a2 2 0 0 1 .02 0" fill="${colors[0]}" stroke="none"></path></svg>`;}
        function fetchData(){fetch('/data').then(r=>r.json()).then(d=>{document.getElementById('deviceName').innerText=d.deviceId;document.getElementById('modalDeviceName').value=d.deviceId;document.getElementById('rssi').innerText=d.rssi;document.getElementById('uid').innerText=d.lastUid;document.getElementById('cardData').innerText=d.lastCardData;document.getElementById('name').innerText=d.lastScannedName;document.getElementById('designation').innerText=d.lastScannedDesignation;document.getElementById('modalRelayDelay').value=d.relayDelay;document.getElementById('modalApiUrlNfc').value=d.apiUrlNfc;document.getElementById('modalApiUrlStatus').value=d.apiUrlStatus;document.getElementById('modalApiUrlUpdate').value=d.apiUrlUpdate;const v=document.getElementById('verification');v.innerText=d.lastVerificationStatus;if(d.lastVerificationStatus==='OK')v.className='badge badge-success';else if(d.lastVerificationStatus==='N/A')v.className='badge badge-ghost';else v.className='badge badge-error';const s=document.getElementById('serverStatus');s.innerText=d.serverStatus;s.className=d.serverStatus==='Active'?'badge badge-success':'badge badge-error';document.getElementById('backupToggle').checked=d.backupModeEnabled;document.getElementById('backupCount').innerText=d.backupCount;updateWifiIcon(d.rssi);});}
        function toggleBackupMode() { fetch('/toggle-backup', { method: 'POST' }).then(() => { setTimeout(fetchData, 250); }); }
        function checkServerNow() { fetch('/check-status', { method: 'POST' }).then(() => { setTimeout(fetchData, 500); }); }
        function saveSettings() {
            const currentPass = prompt("To save settings, please enter your current password:");
            if (currentPass === null) return; // User cancelled
            const form = document.getElementById('settingsForm');
            const formData = new FormData(form);
            formData.append('authPassword', currentPass);
            fetch('/settings', { method: 'POST', body: new URLSearchParams(formData) })
                .then(response => {
                    if (response.ok) {
                        alert("Settings saved successfully!");
                        settings_modal.close();
                        fetchData();
                    } else {
                        alert("Error: Incorrect password or invalid data.");
                    }
                });
        }
        function downloadPDF() {
            fetch('/download-data').then(res => res.json()).then(data => {
                if (!data || data.length === 0) { alert('No backup data to download.'); return; }
                const { jsPDF } = window.jspdf;
                const doc = new jsPDF();
                doc.text("NFC Backup Data", 14, 16);
                doc.autoTable({
                    head: [['#', 'UID', 'Card Data', 'Date', 'Time']],
                    body: data.map((row, i) => [i + 1, row.uid, row.Card_data, row.date, row.time]),
                    startY: 20,
                });
                doc.save('nfc_backup_data.pdf');
            }).catch(err => alert('Error downloading data.'));
        }
        setInterval(fetchData,2000);window.onload=fetchData;
    </script>
</body></html>
)rawliteral";
    server.send(200, "text/html", html);
}
void handleData() {
    if (!isAuthenticated()) { server.send(401); return; }
    JsonDocument doc;
    doc["deviceId"] = deviceId;
    doc["rssi"] = lastRssi;
    doc["relayDelay"] = relayDelay;
    doc["apiUrlNfc"] = apiUrlNfc;
    doc["apiUrlStatus"] = apiUrlStatus;
    doc["apiUrlUpdate"] = apiUrlUpdate;
    doc["serverStatus"] = serverStatus;
    doc["backupModeEnabled"] = backupModeEnabled;
    doc["backupCount"] = backupCount;
    doc["lastUid"] = lastUid;
    doc["lastCardData"] = lastNfcData;
    doc["lastScannedName"] = lastScannedName;
    doc["lastScannedDesignation"] = lastScannedDesignation;
    doc["lastVerificationStatus"] = lastVerificationStatus;
    String jsonString;
    serializeJson(doc, jsonString);
    server.send(200, "application/json", jsonString);
}
void handleSettings() {
    if (!isAuthenticated()) { server.send(401); return; }
    if (!server.hasArg("authPassword") || server.arg("authPassword") != adminPassword) {
        server.send(401, "text/plain", "Incorrect Password");
        return;
    }

    if (server.hasArg("newId") && server.arg("newId").length() > 0) {
        deviceId = server.arg("newId");
        preferences.putString("deviceId", deviceId);
    }
    if (server.hasArg("delay")) {
        relayDelay = server.arg("delay").toInt();
        preferences.putUInt("relayDelay", relayDelay);
    }
    if (server.hasArg("apiUrlNfc") && server.arg("apiUrlNfc").length() > 0) {
        apiUrlNfc = server.arg("apiUrlNfc");
        preferences.putString("apiUrlNfc", apiUrlNfc);
    }
    if (server.hasArg("apiUrlStatus") && server.arg("apiUrlStatus").length() > 0) {
        apiUrlStatus = server.arg("apiUrlStatus");
        preferences.putString("apiUrlStatus", apiUrlStatus);
    }
    if (server.hasArg("apiUrlUpdate") && server.arg("apiUrlUpdate").length() > 0) {
        apiUrlUpdate = server.arg("apiUrlUpdate");
        preferences.putString("apiUrlUpdate", apiUrlUpdate);
    }
    if (server.hasArg("oldPassword") && server.arg("oldPassword") == adminPassword && server.hasArg("newPassword") && server.arg("newPassword").length() > 0) {
        adminPassword = server.arg("newPassword");
        preferences.putString("adminPass", adminPassword);
    }
    server.send(200, "text/plain", "OK");
}
void handleUnlock() {
    if (!isAuthenticated()) { server.send(401); return; }
    playSuccessSound();
    digitalWrite(RELAY_PIN, HIGH);
    delay(relayDelay);
    digitalWrite(RELAY_PIN, LOW);
    server.sendHeader("Location", "/");
    server.send(302);
}
void handleResetWifi() {
    if (!isAuthenticated()) { server.send(401); return; }
    WiFiManager wm;
    wm.resetSettings();
    showMessage("WiFi Reset!", "Restarting...");
    delay(2000);
    ESP.restart();
}
void handleToggleBackup() {
    if (!isAuthenticated()) { server.send(401); return; }
    backupModeEnabled = !backupModeEnabled;
    preferences.putBool("backupMode", backupModeEnabled);
    server.send(200);
}
void handleUpdateAll() {
    if (!isAuthenticated()) { server.send(401); return; }
    manualUpdateRequested = true;
    server.sendHeader("Location", "/");
    server.send(302);
}
void handleDownloadData() {
    if (!isAuthenticated()) { server.send(401); return; }
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();
    if (backupCount > 0) {
        int tempReadIndex = backupReadIndex;
        for (int i = 0; i < backupCount; i++) {
            String key = "scan_" + String(tempReadIndex);
            String jsonData = preferences.getString(key.c_str(), "");
            JsonDocument scanDoc;
            deserializeJson(scanDoc, jsonData);
            array.add(scanDoc);
            tempReadIndex = (tempReadIndex + 1) % MAX_BACKUP_SCANS;
        }
    }
    String jsonString;
    serializeJson(doc, jsonString);
    server.send(200, "application/json", jsonString);
}
void handleCheckStatus() {
    if (!isAuthenticated()) { server.send(401); return; }
    checkServerStatus();
    server.send(200);
}
void handleRestart() {
    if (!isAuthenticated()) { server.send(401); return; }
    showMessage("Restarting...", "By Admin Request");
    server.send(200, "text/plain", "Device is restarting...");
    delay(1000);
    ESP.restart();
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
  adminPassword = preferences.getString("adminPass", "1234");
  apiUrlNfc = preferences.getString("apiUrlNfc", "https://nfc-attenence.onrender.com/api/nfc");
  apiUrlStatus = preferences.getString("apiUrlStatus", "https://nfc-attenence.onrender.com/api/stts");
  apiUrlUpdate = preferences.getString("apiUrlUpdate", "https://nfc-attenence.onrender.com/api/nfcupdat");
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
  server.on("/login", HTTP_GET, handleLogin);
  server.on("/login", HTTP_POST, handleDoLogin);
  server.on("/forgot-password", HTTP_GET, handleForgotPassword);
  server.on("/forgot-password", HTTP_POST, handleDoReset);
  server.on("/data", HTTP_GET, handleData);
  server.on("/settings", HTTP_POST, handleSettings);
  server.on("/unlock", HTTP_POST, handleUnlock);
  server.on("/reset-wifi", HTTP_POST, handleResetWifi);
  server.on("/toggle-backup", HTTP_POST, handleToggleBackup);
  server.on("/update-all", HTTP_POST, handleUpdateAll);
  server.on("/download-data", HTTP_GET, handleDownloadData);
  server.on("/check-status", HTTP_POST, handleCheckStatus);
  server.on("/restart", HTTP_POST, handleRestart);
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

  if (WiFi.status() == WL_CONNECTED && serverStatus == "Active" && backupCount > 0) {
      if (!backupModeEnabled || manualUpdateRequested) {
          sendBackedUpData();
      }
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
        int langCodeLength = payload[0];
        for (int i = 1 + langCodeLength; i < payloadLength; i++) {
            data += (char)payload[i];
        }
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
    doc["Card_data"] = lastNfcData;
    doc["date"] = dateString;
    doc["time"] = timeString;
    String jsonString;
    serializeJson(doc, jsonString);

    if (WiFi.status() != WL_CONNECTED || backupModeEnabled) {
        if (backupCount < MAX_BACKUP_SCANS) {
            animateSaving();
            String key = "scan_" + String(backupWriteIndex);
            preferences.putString(key.c_str(), jsonString);
            backupWriteIndex = (backupWriteIndex + 1) % MAX_BACKUP_SCANS;
            backupCount++;
            preferences.putInt("bkWriteIdx", backupWriteIndex);
            preferences.putInt("bkCount", backupCount);
            showMessage("Saved Locally", "Access Granted");
            playSuccessSound();
            digitalWrite(RELAY_PIN, HIGH);
            delay(relayDelay);
            digitalWrite(RELAY_PIN, LOW);
            delay(2000);
        } else {
            animateFailure();
            showMessage("Backup Full!", "Cannot save scan");
            playFailureSound();
            delay(2000);
        }
    } else {
        animateSending();
        HTTPClient http;
        http.begin(apiUrlNfc);
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
            animateSuccess();
            playSuccessSound();
            displayUserInfoCard(lastScannedName, lastScannedDesignation);
            digitalWrite(RELAY_PIN, HIGH);
            delay(relayDelay);
            digitalWrite(RELAY_PIN, LOW);
          } else {
            animateFailure();
            playFailureSound();
            showMessage("Access Denied", "", 2);
            delay(4000);
          }
        } else {
          animateFailure();
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

// --- Functions for Status and Backup ---
bool isAuthenticated() {
    if (sessionStartTime == 0) return false;
    if (millis() - sessionStartTime > sessionTimeout) {
        sessionStartTime = 0; // Expire the session
        return false;
    }
    return true;
}

void checkServerStatus() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(apiUrlStatus);
        int httpCode = http.GET();
        if (httpCode > 0) {
            String payload = http.getString();
            JsonDocument doc;
            deserializeJson(doc, payload);
            serverStatus = (doc["status"] == "active") ? "Active" : "Inactive";
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

            String apiUrl = manualUpdateRequested ? apiUrlUpdate : apiUrlNfc;

            HTTPClient http;
            http.begin(apiUrl);
            http.addHeader("Content-Type", "application/json");
            int httpResponseCode = http.POST(jsonData);

            if (httpResponseCode > 0) {
                lastApiResponse = http.getString();
                Serial.println("Backup API Response: " + lastApiResponse);
                
                JsonDocument responseDoc;
                deserializeJson(responseDoc, lastApiResponse);
                lastScannedName = responseDoc["name"].as<String>();
                lastScannedDesignation = responseDoc["designation"].as<String>();
                lastVerificationStatus = responseDoc["verify"].as<String>();

                animateSuccess();
                if (lastVerificationStatus == "OK") {
                    displayUserInfoCard(lastScannedName, lastScannedDesignation);
                } else {
                    showMessage("Update OK", "User Not Verified");
                }
                delay(3000);

                preferences.remove(key.c_str());
                backupReadIndex = (backupReadIndex + 1) % MAX_BACKUP_SCANS;
                backupCount--;
                preferences.putInt("bkReadIdx", backupReadIndex);
                preferences.putInt("bkCount", backupCount);
                Serial.println("Backup scan sent successfully.");
            } else {
                Serial.println("Failed to send backup scan, will retry.");
                manualUpdateRequested = false;
            }
            http.end();
        }
        
        if (backupCount == 0) {
            manualUpdateRequested = false;
        }
    } else {
        manualUpdateRequested = false;
    }
}
