#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Yapılandırma dosyası
const char* CONFIG_FILE = "/config.json";

// Ayarlar için yeni struct
struct Settings {
  int moistureThreshold = 50;
  int wateringDuration = 10;
  int lightOnHour = 8;
  int lightOnMinute = 0;
  int lightOffHour = 20;
  int lightOffMinute = 0;
};

Settings settings;

const char* ssid = "Wifi";
const char* password = "pass";

ESP8266WebServer server(80);

const int soilMoisturePin = A0;
const int relayPinWater = D1;
const int relayPinLight = D2;
const int statusLed = LED_BUILTIN;  // Dahili LED'i kullanacağız

int soilMoistureValue = 0;
bool manualWatering = false;
bool manualLighting = false;

const int airValue = 1023;    // Havadaki değer
const int waterValue = 0;     // Sudaki değer

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600 * 3); // UTC+3 için
const char* LOG_FILE = "/events.log";
bool isTimeSet = false;
time_t lastKnownTime = 0;

// Global değişkenlere eklemeler
unsigned long lastNTPSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 3600000; // 1 saat


// Global değişkenlere ekle
const char* EDIT_FORM = "<form id='editForm' method='POST' action='/saveFile'>"
                       "<input type='hidden' name='filename' value='%s'>"
                       "<textarea name='content' rows='20' style='width:100%%'>%s</textarea>"
                       "<button type='submit' class='button'><i class='fas fa-save'></i> Kaydet</button>"
                       "<button type='button' class='button' onclick='window.location=\"/files\"'>"
                       "<i class='fas fa-times'></i> İptal</button></form>";

// Global değişkenlere ekleme
bool timeInitialized = false; // Zaman başlatma kontrolü için

// getNtpTime fonksiyonunu güncelle
time_t getNtpTime() {
  if (timeClient.update()) {
    time_t ntpTime = timeClient.getEpochTime();
    if (ntpTime > 1600000000) { // 2020 yılından sonrası geçerli
      timeInitialized = true;
      return ntpTime;
    }
  }
  return 0;
}

// Ayarları kaydetme fonksiyonu
void saveSettings() {
  DynamicJsonDocument doc(512);
  doc["moistureThreshold"] = settings.moistureThreshold;
  doc["wateringDuration"] = settings.wateringDuration;
  doc["lightOnHour"] = settings.lightOnHour;
  doc["lightOnMinute"] = settings.lightOnMinute;
  doc["lightOffHour"] = settings.lightOffHour;
  doc["lightOffMinute"] = settings.lightOffMinute;

  File configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println("Yapılandırma dosyası açılamadı!");
    return;
  }

  serializeJson(doc, configFile);
  configFile.close();
}

// Ayarları yükleme fonksiyonu
void loadSettings() {
  if (LittleFS.exists(CONFIG_FILE)) {
    File configFile = LittleFS.open(CONFIG_FILE, "r");
    if (configFile) {
      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, configFile);
      
      if (!error) {
        settings.moistureThreshold = doc["moistureThreshold"] | 50;
        settings.wateringDuration = doc["wateringDuration"] | 10;
        settings.lightOnHour = doc["lightOnHour"] | 8;
        settings.lightOnMinute = doc["lightOnMinute"] | 0;
        settings.lightOffHour = doc["lightOffHour"] | 20;
        settings.lightOffMinute = doc["lightOffMinute"] | 0;
      }
      configFile.close();
    }
  }
}

// logEvent fonksiyonunu güncelle
void logEvent(const char* event, bool offline = false) {
  String timeStr;
  time_t currentTime;
  
  if (timeInitialized) {
    currentTime = now();
    struct tm* timeinfo = localtime(&currentTime);
    char buffer[25];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    timeStr = String(buffer);
  } else {
    currentTime = lastKnownTime > 1600000000 ? lastKnownTime : now();
    struct tm* timeinfo = localtime(&currentTime);
    char buffer[25];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    timeStr = String(buffer) + " (Offline)";
    lastKnownTime = currentTime + 1;
  }

  String logEntry = timeStr + ": " + event + "\n";
  File logFile = LittleFS.open(LOG_FILE, "a");
  if (logFile) {
    logFile.print(logEntry);
    logFile.close();
  }
}

String getLogsByDate(String date) {
  String logs;
  File logFile = LittleFS.open(LOG_FILE, "r");
  if (!logFile) {
    return "";
  }

  while (logFile.available()) {
    String line = logFile.readStringUntil('\n');
    if (line.startsWith(date)) {
      // Tarih formatını düzelt
      int spacePos = line.indexOf(' ');
      if (spacePos > 0) {
        String timestamp = line.substring(0, spacePos + 9); // HH:MM:SS'e kadar
        String message = line.substring(spacePos + 10);
        logs += "<div class='log-entry'>";
        logs += "<span class='log-time'>" + timestamp + "</span> - ";
        logs += "<span class='log-message'>" + message + "</span>";
        logs += "</div>";
      }
    }
  }
  logFile.close();
  return logs;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nBaşlatılıyor...");
  
  // LittleFS başlatma
  if (!LittleFS.begin()) {
    Serial.println("LittleFS başlatılamadı!");
    return;
  }

  // Ayarları yükle
  loadSettings();

  // Pin ayarları
  pinMode(statusLed, OUTPUT);
  pinMode(relayPinWater, OUTPUT);
  pinMode(relayPinLight, OUTPUT);
  digitalWrite(statusLed, HIGH);
  digitalWrite(relayPinWater, HIGH);
  digitalWrite(relayPinLight, HIGH);

  // WiFiManager
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180); // 3 dakika timeout
  
  if (!wifiManager.autoConnect("BitkiSulama_AP")) {
    Serial.println("Bağlantı başarısız!");
    ESP.restart();
  }

  // Kalan setup kodu
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("WiFi'ya bağlanılıyor");

  // WiFi bağlantısını bekle
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
    digitalWrite(statusLed, !digitalRead(statusLed));
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(statusLed, LOW);
    Serial.println("WiFi bağlandı!");
    Serial.print("IP adresi: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC adresi: ");
    Serial.println(WiFi.macAddress());
    Serial.print("Sinyal gücü: ");
    Serial.println(WiFi.RSSI());
  } else {
    digitalWrite(statusLed, HIGH);
    Serial.println("WiFi bağlantısı başarısız!");
    ESP.restart();
  }

  // MDNS başlatma
  if (!MDNS.begin("bitki")) {
    Serial.println("MDNS başlatılamadı!");
    return;
  }
  Serial.println("MDNS başlatıldı - http://bitki.local");

  // Web server rotaları
  server.on("/", HTTP_GET, []() {
    Serial.println("Ana sayfa istendi");
    handleRoot();
  });
  
  server.on("/status", HTTP_GET, []() {
    Serial.println("Durum istendi");
    handleStatus();
  });
  
  server.on("/water", HTTP_GET, []() {
    Serial.println("Sulama istendi");
    handleWater();
  });
  
  server.on("/light", HTTP_GET, []() {
    Serial.println("Işık kontrolü istendi");
    handleLight();
  });
  
  server.on("/settings", HTTP_GET, []() {
    Serial.println("Ayarlar istendi");
    handleSettings();
  });
  
  server.on("/updateSettings", HTTP_POST, []() {
    Serial.println("Ayarlar güncelleme istendi");
    handleUpdateSettings();
  });

  server.on("/logs", HTTP_GET, []() {
    Serial.println("Loglar istendi");
    handleLogs();
  });

  server.on("/files", HTTP_GET, handleFileList);
  server.on("/edit", HTTP_GET, handleFileEdit);
  server.on("/saveFile", HTTP_POST, handleFileSave);
  server.on("/delete", HTTP_GET, handleFileDelete);
  server.on("/rename", HTTP_POST, handleFileRename);

  // Web server başlatma
  server.begin();
  Serial.println("HTTP server başlatıldı");
  
  // OTA güncelleme ayarları
  ArduinoOTA.onStart([]() {
    Serial.println("OTA başlatılıyor...");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("İlerleme: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.begin();
  Serial.println("Sistem hazır!");

  // NTP başlatma kısmını güncelle
  timeClient.begin();
  setSyncProvider(getNtpTime);
  setSyncInterval(NTP_SYNC_INTERVAL);
  
  // İlk zaman senkronizasyonu
  if (WiFi.status() == WL_CONNECTED) {
    time_t ntpTime = getNtpTime();
    if (ntpTime > 0) {
      setTime(ntpTime);
      lastKnownTime = ntpTime;
      logEvent("Sistem başlatıldı");
    } else {
      logEvent("Sistem başlatıldı (Zaman senkronizasyonu başarısız)");
    }
  } else {
    logEvent("Sistem başlatıldı (Offline)");
  }
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  MDNS.update();

  // WiFi bağlantı kontrolü
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) {  // Her 30 saniyede bir kontrol
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi bağlantısı koptu! Yeniden bağlanılıyor...");
      WiFi.reconnect();
    }
  }

  // Nem ölçümü ve sulama kontrolü
  static unsigned long lastSoilCheck = 0;
  if (millis() - lastSoilCheck > 5000) {  // Her 5 saniyede bir kontrol
    lastSoilCheck = millis();
    soilMoistureValue = analogRead(soilMoisturePin);
    Serial.printf("Toprak Nemi: %d\n", soilMoistureValue);
    
    if (!manualWatering && soilMoistureValue < settings.moistureThreshold) {
      logEvent("Otomatik sulama başlatıldı");
      Serial.println("Otomatik sulama başlatılıyor...");
      digitalWrite(relayPinWater, LOW);
      delay(settings.wateringDuration * 1000);
      digitalWrite(relayPinWater, HIGH);
      Serial.println("Sulama tamamlandı");
      logEvent("Otomatik sulama tamamlandı");
    }
  }

  // Işık kontrolü için zaman kontrolü
  time_t currentTime = now(); // TimeLib'in now() fonksiyonu
  struct tm* timeinfo = localtime(&currentTime);
  
  bool shouldLightBeOn = false;
  int currentMinutes = timeinfo->tm_hour * 60 + timeinfo->tm_min;
  int onMinutes = settings.lightOnHour * 60 + settings.lightOnMinute;
  int offMinutes = settings.lightOffHour * 60 + settings.lightOffMinute;
  
  if (onMinutes < offMinutes) {
    shouldLightBeOn = (currentMinutes >= onMinutes && currentMinutes < offMinutes);
  } else {
    shouldLightBeOn = (currentMinutes >= onMinutes || currentMinutes < offMinutes);
  }

  if (!manualLighting) {
    digitalWrite(relayPinLight, shouldLightBeOn ? LOW : HIGH);
  }

  // Zaman senkronizasyonu kontrolü
  if (millis() - lastNTPSync >= NTP_SYNC_INTERVAL) {
    lastNTPSync = millis();
    if (WiFi.status() == WL_CONNECTED) {
      time_t ntpTime = getNtpTime();
      if (ntpTime > 0) {
        setTime(ntpTime);
        isTimeSet = true;
        lastKnownTime = ntpTime;
      }
    }
  }
}

int calculateMoisturePercentage(int rawValue) {
  int percentage = map(rawValue, airValue, waterValue, 0, 100);
  percentage = constrain(percentage, 0, 100);
  return percentage;
}

// handleRoot fonksiyonundaki JavaScript bölümünü güncelle
void handleRoot() {
  String html = "<html><head><meta charset='UTF-8'><title>Akıllı Sulama Sistemi</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.4/css/all.min.css'>";
  html += "<style>";
  html += "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background-color: #f0f2f5; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  html += "h1 { color: #1a73e8; text-align: center; margin-bottom: 30px; }";
  html += "h2 { color: #3c4043; margin-top: 0; }";
  html += ".nav-dots { text-align: center; margin: 20px 0; background: #fff; padding: 10px; border-radius: 25px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }";
  html += ".nav-dot { display: inline-flex; align-items: center; justify-content: center; height: 40px; min-width: 40px; margin: 0 5px; background-color: #f8f9fa; border-radius: 20px; cursor: pointer; transition: all 0.3s ease; padding: 0 15px; }";
  html += ".nav-dot.active { background-color: #1a73e8; color: white; }";
  html += ".slide { display: none; padding: 20px; animation: fadeIn 0.5s; }";
  html += ".status-card { background: #e8f0fe; padding: 20px; border-radius: 12px; margin: 10px 0; }";
  html += ".status-item { display: flex; align-items: center; margin: 15px 0; }";
  html += ".status-icon { font-size: 24px; margin-right: 15px; color: #1a73e8; }";
  html += ".button { display: inline-flex; align-items: center; background: #1a73e8; color: white; border: none; padding: 12px 24px; border-radius: 25px; cursor: pointer; margin: 10px; transition: all 0.3s ease; font-size: 16px; }";
  html += ".button i { margin-right: 8px; }";
  html += ".button:hover { background: #1557b0; transform: translateY(-2px); box-shadow: 0 2px 5px rgba(0,0,0,0.2); }";
  html += ".settings-form { display: grid; gap: 20px; }";
  html += ".form-group { background: #f8f9fa; padding: 15px; border-radius: 12px; }";
  html += "label { color: #3c4043; font-weight: 500; display: block; margin-bottom: 8px; }";
  html += "input { width: 100%; padding: 10px; border: 2px solid #e0e0e0; border-radius: 8px; transition: border-color 0.3s; }";
  html += "input:focus { border-color: #1a73e8; outline: none; }";
  html += "@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }";
  html += ".log-container { padding: 15px; }";
  html += ".log-day { margin-bottom: 20px; background: #f8f9fa; padding: 15px; border-radius: 12px; }";
  html += ".log-day h3 { color: #1a73e8; margin-top: 0; }";
  html += ".log-entries { font-family: monospace; line-height: 1.5; }";
  html += ".log-entry { padding: 5px 0; border-bottom: 1px solid #e0e0e0; }";
  html += ".log-time { color: #666; font-weight: bold; }";
  html += ".log-message { color: #333; }";
  html += ".log-day { margin-bottom: 20px; background: #fff; padding: 15px; border-radius: 12px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }";
  html += ".log-entries { max-height: 300px; overflow-y: auto; padding: 10px; background: #f8f9fa; border-radius: 8px; }";
  html += ".file-list { background: #fff; border-radius: 12px; padding: 15px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }";
  html += ".file-item { display: flex; align-items: center; padding: 10px; border-bottom: 1px solid #eee; }";
  html += ".file-icon { font-size: 20px; margin-right: 10px; color: #1a73e8; }";
  html += ".file-name { flex-grow: 1; }";
  html += ".file-actions { display: flex; gap: 10px; }";
  html += ".rename-form { display: none; }";
  html += "textarea { width: 100%; height: 70vh; font-family: monospace; padding: 10px; }";
  html += ".status-container { display: flex; flex-direction: column; align-items: center; padding: 20px; }";
  html += ".plant-container { position: relative; width: 300px; height: 300px; margin: 20px auto; }";
  html += ".plant-image { position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); width: 200px; height: 200px; }";
  html += ".moisture-indicator { position: absolute; bottom: 0; left: 50%; transform: translateX(-50%); width: 100px; height: 200px; background: #e8f0fe; border-radius: 10px; overflow: hidden; }";
  html += ".moisture-level { position: absolute; bottom: 0; width: 100%; background: #1a73e8; transition: height 0.5s ease; }";
  html += ".moisture-text { position: absolute; width: 100%; text-align: center; top: 50%; transform: translateY(-50%); color: #000; font-weight: bold; text-shadow: 1px 1px 1px rgba(255,255,255,0.5); }";
  html += ".sun { position: absolute; top: 20px; right: 20px; width: 60px; height: 60px; }";
  html += ".sun.active { animation: glow 2s infinite; }";
  html += "@keyframes glow { 0% { filter: brightness(1); } 50% { filter: brightness(1.3); } 100% { filter: brightness(1); } }";
  html += ".status-icons { display: flex; justify-content: space-around; width: 100%; margin-top: 20px; }";
  html += ".status-icon-container { text-align: center; padding: 15px; }";
  html += ".status-icon-container i { font-size: 30px; margin-bottom: 10px; }";
  html += ".status-value { font-size: 18px; font-weight: bold; }";
  html += ".status-label { font-size: 14px; color: #666; }";
  html += "</style>";
  
  html += "<script>";
  html += "let currentSlide = 0;";
  html += "function showSlide(n) { currentSlide = n; updateSlides(); if(n === 3) loadLogs(); if(n === 4) loadFileManager(); }";
  html += "function updateSlides() {";
  html += "  const slides = document.getElementsByClassName('slide');";
  html += "  const dots = document.getElementsByClassName('nav-dot');";
  html += "  for (let i = 0; i < slides.length; i++) {";
  html += "    slides[i].style.display = i === currentSlide ? 'block' : 'none';";
  html += "    dots[i].className = i === currentSlide ? 'nav-dot active' : 'nav-dot';";
  html += "  }";
  html += "}";
  html += "function loadStatus() {";
  html += "  fetch('/status').then(response => response.text()).then(data => {";
  html += "    document.getElementById('status').innerHTML = data;";
  html += "  });";
  html += "}";
  html += "function toggleWater() { fetch('/water?manual=1').then(() => loadStatus()); }";
  html += "function toggleLight() { fetch('/light?manual=1').then(() => loadStatus()); }";
  html += "setInterval(loadStatus, 5000);";
  html += "document.addEventListener('DOMContentLoaded', function() { showSlide(0); loadStatus(); if(currentSlide === 4) loadFileManager(); });";
  html += "function loadLogs() {";
  html += "  fetch('/logs').then(response => response.text()).then(data => {";
  html += "    document.getElementById('logs').innerHTML = data;";
  html += "    const select = document.getElementById('dateSelect');";
  html += "    if (select) {";
  html += "      showLogsForDate(select.value);";
  html += "    }";
  html += "  }).catch(error => console.error('Log yükleme hatası:', error));";
  html += "}";
  html += "function showRename(filename) {";
  html += "  document.getElementById('rename-'+filename).style.display = 'inline-flex';";
  html += "  document.getElementById('name-'+filename).style.display = 'none';";
  html += "}";
  html += "function loadFileManager() {";
  html += "  const container = document.getElementById('fileManagerContainer');";
  html += "  if(!container) return;";
  html += "  container.innerHTML = '<div class=\"loading\">Yükleniyor...</div>';";

  html += "  fetch('/files')";
  html += "    .then(response => response.text())";
  html += "    .then(data => {";
  html += "      container.innerHTML = data;";
  html += "    })";
  html += "    .catch(error => {";
  html += "      container.innerHTML = '<div class=\"error\">Yükleme hatası: ' + error + '</div>';";

  html += "    });";
  html += "}";

  // showLogsForDate fonksiyonunu ana sayfaya ekle
  html += "function showLogsForDate(date) {";
  html += "  const logDays = document.getElementsByClassName('log-day');";
  html += "  Array.from(logDays).forEach(day => {";
  html += "    if(date === 'all' || day.getAttribute('data-date') === date) {";
  html += "      day.style.display = 'block';";
  html += "    } else {";
  html += "      day.style.display = 'none';";
  html += "    }";
  html += "  });";
  html += "}";

  html += "</script>";
  
  html += "</head><body>";
  
  html += "<div class='container'>";
  html += "<h1><i class='fas fa-leaf'></i> Akıllı Sulama Sistemi</h1>";
  
  html += "<div class='nav-dots'>";
  html += "<span class='nav-dot' onclick='showSlide(0)'><i class='fas fa-tachometer-alt'></i> Durum</span>";
  html += "<span class='nav-dot' onclick='showSlide(1)'><i class='fas fa-cogs'></i> Kontrol</span>";
  html += "<span class='nav-dot' onclick='showSlide(2)'><i class='fas fa-sliders-h'></i> Ayarlar</span>";
  html += "<span class='nav-dot' onclick='showSlide(3)'><i class='fas fa-history'></i> Kayıtlar</span>";
  html += "<span class='nav-dot' onclick='showSlide(4)'><i class='fas fa-folder'></i> Dosyalar</span>";
  html += "</div>";
  
  html += "<div class='slide'>";
  html += "<h2><i class='fas fa-chart-bar'></i> Sistem Durumu</h2>";
  html += "<div id='status' class='status-card'>Yükleniyor...</div>";
  html += "</div>";
  
  html += "<div class='slide'>";
  html += "<h2><i class='fas fa-gamepad'></i> Manuel Kontrol</h2>";
  html += "<button class='button' onclick='toggleWater()'><i class='fas fa-tint'></i> Manuel Sulama</button>";
  html += "<button class='button' onclick='toggleLight()'><i class='fas fa-lightbulb'></i> Işıkları Aç/Kapat</button>";
  html += "</div>";
  
  html += "<div class='slide'>";
  html += "<h2><i class='fas fa-cog'></i> Ayarlar</h2>";
  html += "<form class='settings-form' action='/updateSettings' method='POST'>";
  html += "<div class='form-group'>";
  html += "<label><i class='fas fa-tint'></i> Nem Eşik Değeri (%):</label>";
  html += "<input type='number' name='threshold' value='" + String(settings.moistureThreshold) + "' min='0' max='100'>";
  html += "</div>";
  html += "<div class='form-group'>";
  html += "<label><i class='fas fa-clock'></i> Sulama Süresi (saniye):</label>";
  html += "<input type='number' name='duration' value='" + String(settings.wateringDuration) + "' min='1' max='60'>";
  html += "</div>";
  html += "<div class='form-group'>";
  html += "<label><i class='fas fa-sun'></i> Işık Açılma Zamanı:</label>";
  html += "<div class='time-inputs'>";
  html += "<input type='number' name='lightOnHour' value='" + String(settings.lightOnHour) + "' min='0' max='23' style='width:70px'> : ";
  html += "<input type='number' name='lightOnMinute' value='" + String(settings.lightOnMinute) + "' min='0' max='59' style='width:70px'>";
  html += "</div></div>";
  
  html += "<div class='form-group'>";
  html += "<label><i class='fas fa-moon'></i> Işık Kapanma Zamanı:</label>";
  html += "<div class='time-inputs'>";
  html += "<input type='number' name='lightOffHour' value='" + String(settings.lightOffHour) + "' min='0' max='23' style='width:70px'> : ";
  html += "<input type='number' name='lightOffMinute' value='" + String(settings.lightOffMinute) + "' min='0' max='59' style='width:70px'>";
  html += "</div></div>";
  html += "<button type='submit' class='button'><i class='fas fa-save'></i> Ayarları Kaydet</button>";
  html += "</form>";
  html += "</div>";

  html += "<div class='slide'>";
  html += "<div id='logs'>Yükleniyor...</div>";
  html += "</div>";

  html += "<div class='slide'>";
  html += "<h2><i class='fas fa-folder-open'></i> Dosya Yöneticisi</h2>";
  html += "<div id='fileManagerContainer'></div>";
  html += "</div>";

  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

// handleStatus fonksiyonunu güncelle
void handleStatus() {
  int moisturePercentage = calculateMoisturePercentage(soilMoistureValue);
  
  String status = "<div class='status-container'>";
  
  // Plant container with moisture indicator
  status += "<div class='plant-container'>";
  // Güneş ikonu (ışıklar açıksa aktif class'ı ekle)
  status += "<div class='sun" + String(manualLighting ? " active" : "") + "'>";
  status += "<i class='fas fa-sun' style='color: #ffd700; font-size: 60px;'></i>";
  status += "</div>";
  
  // Bitki görseli
  status += "<div class='plant-image'>";
  status += "<i class='fas fa-seedling' style='color: #4CAF50; font-size: 200px;'></i>";
  status += "</div>";
  
  // Nem göstergesi
  status += "<div class='moisture-indicator'>";
  status += "<div class='moisture-level' style='height: " + String(moisturePercentage) + "%;'></div>";
  status += "<div class='moisture-text'>" + String(moisturePercentage) + "%</div>";
  status += "</div>";
  status += "</div>";
  
  // Durum ikonları
  status += "<div class='status-icons'>";
  
  // Nem durumu
  status += "<div class='status-icon-container'>";
  status += "<i class='fas fa-tint' style='color: #1a73e8;'></i>";
  status += "<div class='status-value'>" + String(moisturePercentage) + "%</div>";
  status += "<div class='status-label'>Toprak Nemi</div>";
  status += "</div>";
  
  // Sulama durumu
  status += "<div class='status-icon-container'>";
  status += "<i class='fas fa-hand-holding-water' style='color: " + String(manualWatering ? "#4CAF50" : "#666") + ";'></i>";
  status += "<div class='status-value'>" + String(manualWatering ? "Aktif" : "Pasif") + "</div>";
  status += "<div class='status-label'>Sulama Durumu</div>";
  status += "</div>";
  
  // Işık durumu
  status += "<div class='status-icon-container'>";
  status += "<i class='fas fa-lightbulb' style='color: " + String(manualLighting ? "#ffd700" : "#666") + ";'></i>";
  status += "<div class='status-value'>" + String(manualLighting ? "Açık" : "Kapalı") + "</div>";
  status += "<div class='status-label'>Işık Durumu</div>";
  status += "</div>";
  
  status += "</div>";
  status += "</div>";
  
  server.send(200, "text/html", status);
}

void handleWater() {
  if (server.hasArg("manual")) {
    logEvent("Manuel sulama başlatıldı");
    manualWatering = true;
    digitalWrite(relayPinWater, LOW);
    delay(settings.wateringDuration * 1000);
    digitalWrite(relayPinWater, HIGH);
    manualWatering = false;
    logEvent("Manuel sulama tamamlandı");
  }
  server.send(200, "text/plain", "Watering done");
}

void handleLight() {
  if (server.hasArg("manual")) {
    manualLighting = !manualLighting;
    digitalWrite(relayPinLight, manualLighting ? LOW : HIGH);
    logEvent(manualLighting ? "Işıklar manuel açıldı" : "Işıklar manuel kapatıldı");
  }
  server.send(200, "text/plain", "Lighting toggled");
}

void handleSettings() {
  if (server.hasArg("threshold")) {
    settings.moistureThreshold = server.arg("threshold").toInt();
  }
  if (server.hasArg("duration")) {
    settings.wateringDuration = server.arg("duration").toInt();
  }
  server.send(200, "text/plain", "Settings updated");
}

void handleUpdateSettings() {
  if (server.hasArg("threshold")) {
    settings.moistureThreshold = server.arg("threshold").toInt();
  }
  if (server.hasArg("duration")) {
    settings.wateringDuration = server.arg("duration").toInt();
  }
  if (server.hasArg("lightOnHour") && server.hasArg("lightOnMinute")) {
    settings.lightOnHour = server.arg("lightOnHour").toInt();
    settings.lightOnMinute = server.arg("lightOnMinute").toInt();
  }
  if (server.hasArg("lightOffHour") && server.hasArg("lightOffMinute")) {
    settings.lightOffHour = server.arg("lightOffHour").toInt();
    settings.lightOffMinute = server.arg("lightOffMinute").toInt();
  }
  
  saveSettings();
  server.send(200, "text/plain", "Ayarlar kaydedildi");
}

// handleLogs fonksiyonunu güncelle
void handleLogs() {
  String html = "<div class='log-container'>";
  html += "<h2><i class='fas fa-history'></i> Sistem Kayıtları</h2>";
  
  if (!LittleFS.exists(LOG_FILE)) {
    html += "<div class='log-day'><p>Henüz kayıt bulunmuyor.</p></div>";
    server.send(200, "text/html", html + "</div>");
    return;
  }

  // Bugünün tarihini al
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char todayStr[11];
  strftime(todayStr, sizeof(todayStr), "%Y-%m-%d", timeinfo);
  String today = String(todayStr);

  // Mevcut tarihleri topla
  std::vector<String> availableDates;
  File logFile = LittleFS.open(LOG_FILE, "r");
  if (logFile) {
    while (logFile.available()) {
      String line = logFile.readStringUntil('\n');
      if (line.length() > 10) {
        String date = line.substring(0, 10); // YYYY-MM-DD
        if (std::find(availableDates.begin(), availableDates.end(), date) == availableDates.end()) {
          availableDates.push_back(date);
        }
      }
    }
    logFile.close();
  }

  // Tarihleri sırala (yeniden eskiye)
  std::sort(availableDates.begin(), availableDates.end(), std::greater<String>());

  // CSS ekleme
  html += "<style>";
  html += ".date-selector { margin-bottom: 20px; }";
  html += ".date-select { padding: 8px; border-radius: 8px; width: 200px; }";
  html += "</style>";

  // JavaScript fonksiyonlarını güncelle
  html += "<script>";
  html += "document.addEventListener('DOMContentLoaded', function() {";
  html += "  const today = '" + today + "';";
  html += "  const select = document.getElementById('dateSelect');";
  html += "  if (select) {";
  html += "    const hasToday = Array.from(select.options).some(opt => opt.value === today);";
  html += "    select.value = hasToday ? today : 'all';";
  html += "    if (typeof showLogsForDate === 'function') {";  // Fonksiyon kontrolü eklendi
  html += "      showLogsForDate(select.value);";
  html += "    }";
  html += "  }";
  html += "});";
  html += "</script>";

  // Tarih seçici ekle
  html += "<div class='date-selector'>";
  html += "<select id='dateSelect' onchange='showLogsForDate(this.value)' class='date-select'>";
  html += "<option value='all'>Tüm Tarihler</option>";
  for (const String& date : availableDates) {
    html += "<option value='" + date + "'" + (date == today ? " selected" : "") + ">" + date + "</option>";
  }
  html += "</select></div>";

  // Log içeriği için container
  html += "<div id='log-content'>";
  
  // Tüm logları göster
  for (const String& date : availableDates) {
    String logs = getLogsByDate(date);
    if (logs.length() > 0) {
      html += "<div class='log-day' data-date='" + date + "' style='display: " + (date == today ? "block" : "none") + "'>";
      html += "<h3>" + date + "</h3>";
      html += "<div class='log-entries'>" + logs + "</div>";
      html += "</div>";
    }
  }

  if (availableDates.empty()) {
    html += "<div class='log-day'><p>Kayıt bulunmuyor.</p></div>";
  }

  html += "</div>"; // log-content end
  html += "</div>";
  server.send(200, "text/html", html);
}

void handleFileList() {
  String html = "<!DOCTYPE html>";  // DOCTYPE eklendi
  html += "<html><head><meta charset='UTF-8'>";
  html += "<title>Dosya Yöneticisi</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.4/css/all.min.css'>";
  html += "<style>";
  html += "* { box-sizing: border-box; margin: 0; padding: 0; }";
  html += "body { font-family: 'Segoe UI', sans-serif; line-height: 1.6; padding: 15px; }";
  html += ".file-manager { max-width: 100%; margin: 0 auto; }";
  html += ".file-list { background: #f8f9fa; border-radius: 12px; padding: 15px; margin-top: 15px; }";
  html += ".file-item { display: flex; align-items: center; padding: 12px; background: #fff; margin-bottom: 8px; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }";
  html += ".file-icon { font-size: 20px; margin-right: 15px; color: #1a73e8; }";
  html += ".file-name { flex-grow: 1; }";
  html += ".file-size { color: #666; margin: 0 15px; }";
  html += ".file-actions { display: flex; gap: 8px; }";
  html += ".button { display: inline-flex; align-items: center; padding: 8px 16px; border: none; border-radius: 20px; background: #1a73e8; color: white; cursor: pointer; text-decoration: none; font-size: 14px; }";
  html += ".button:hover { background: #1557b0; }";
  html += ".button i { margin-right: 5px; }";
  html += ".button.delete { background: #dc3545; }";
  html += ".button.delete:hover { background: #bb2d3b; }";
  html += ".rename-form { display: none; position: absolute; background: white; padding: 15px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += ".upload-section { background: #fff; padding: 20px; border-radius: 12px; margin-bottom: 20px; }";
  html += ".upload-section h3 { margin-bottom: 15px; }";
  html += ".file-input { margin-right: 10px; }";
  html += "</style>";

  // Dosya yöneticisi içeriği
  html += "<div class='file-manager'>";
  html += "<div class='upload-section'>";
  html += "<h3><i class='fas fa-upload'></i> Dosya Yükle</h3>";
  html += "<form action='/upload' method='POST' enctype='multipart/form-data'>";
  html += "<input type='file' name='file' class='file-input'>";
  html += "<button type='submit' class='button'><i class='fas fa-upload'></i> Yükle</button>";
  html += "</form></div>";
  
  // Dosya listesi
  html += "<div class='file-list'>";
  Dir dir = LittleFS.openDir("/");
  bool hasFiles = false;
  
  while (dir.next()) {
    hasFiles = true;
    String fileName = dir.fileName();
    String fileSize = String(dir.fileSize());
    String fileIcon = fileName.endsWith(".txt") || fileName.endsWith(".log") ? "fa-file-alt" : "fa-file";
    
    html += "<div class='file-item'>";
    html += "<i class='fas " + fileIcon + " file-icon'></i>";
    html += "<span class='file-name'>" + fileName + "</span>";
    html += "<span class='file-size'>" + fileSize + " bytes</span>";
    
    html += "<div class='file-actions'>";
    if (fileName.endsWith(".txt") || fileName.endsWith(".log")) {
      html += "<a href='/edit?file=" + fileName + "' class='button'>";
      html += "<i class='fas fa-edit'></i> Düzenle</a>";
    }
    html += "<button onclick='showRename(\"" + fileName + "\")' class='button'>";
    html += "<i class='fas fa-pencil-alt'></i> Yeniden Adlandır</button>";
    html += "<a href='/delete?file=" + fileName + "' class='button delete' onclick='return confirm(\"Dosyayı silmek istediğinize emin misiniz?\")'>";
    html += "<i class='fas fa-trash-alt'></i> Sil</a>";
    html += "</div>";
    
    html += "<form id='rename-" + fileName + "' class='rename-form' action='/rename' method='POST'>";
    html += "<input type='hidden' name='oldName' value='" + fileName + "'>";
    html += "<input type='text' name='newName' value='" + fileName + "'>";
    html += "<button type='submit' class='button'><i class='fas fa-check'></i> Kaydet</button>";
    html += "</form>";
    html += "</div>";
  }
  
  if (!hasFiles) {
    html += "<div class='file-item' style='justify-content: center; color: #666;'>";
    html += "<i class='fas fa-info-circle' style='margin-right: 10px;'></i> Henüz dosya bulunmuyor</div>";
  }
  
  html += "</div></div>";
  
  // JavaScript
  html += "<script>";
  html += "function showRename(filename) {";
  html += "  const form = document.getElementById('rename-' + filename);";
  html += "  const allForms = document.getElementsByClassName('rename-form');";
  html += "  Array.from(allForms).forEach(f => f.style.display = 'none');";
  html += "  if(form) form.style.display = 'block';";
  html += "}";
  html += "</script>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleFileEdit() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Dosya adı belirtilmedi");
    return;
  }
  
  String fileName = server.arg("file");
  if (!LittleFS.exists(fileName)) {
    server.send(404, "text/plain", "Dosya bulunamadı");
    return;
  }
  
  File file = LittleFS.open(fileName, "r");
  if (!file) {
    server.send(500, "text/plain", "Dosya açılamadı");
    return;
  }
  
  String content = file.readString();
  file.close();
  
  String html = "<html><head><meta charset='UTF-8'>";
  html += "<title>Dosya Düzenle</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.4/css/all.min.css'>";
  html += "<style>";
  html += "textarea { width: 100%; height: 70vh; font-family: monospace; padding: 10px; }";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1><i class='fas fa-edit'></i> Dosya Düzenle: " + fileName + "</h1>";
  
  // Form oluşturmayı düzelt
  char* formattedForm = new char[strlen(EDIT_FORM) + fileName.length() + content.length() + 1];
  sprintf(formattedForm, EDIT_FORM, fileName.c_str(), content.c_str());
  html += formattedForm;
  delete[] formattedForm;
  
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleFileSave() {
  if (!server.hasArg("filename") || !server.hasArg("content")) {
    server.send(400, "text/plain", "Eksik parametreler");
    return;
  }
  
  String fileName = server.arg("filename");
  String content = server.arg("content");
  
  File file = LittleFS.open(fileName, "w");
  if (!file) {
    server.send(500, "text/plain", "Dosya kaydedilemedi");
    return;
  }
  
  file.print(content);
  file.close();
  
  server.sendHeader("Location", "/files");
  server.send(303);
}

void handleFileDelete() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Dosya adı belirtilmedi");
    return;
  }
  
  String fileName = server.arg("file");
  if (LittleFS.remove(fileName)) {
    server.sendHeader("Location", "/files");
    server.send(303);
  } else {
    server.send(500, "text/plain", "Dosya silinemedi");
  }
}

void handleFileRename() {
  if (!server.hasArg("oldName") || !server.hasArg("newName")) {
    server.send(400, "text/plain", "Eksik parametreler");
    return;
  }
  
  String oldName = server.arg("oldName");
  String newName = server.arg("newName");
  
  if (LittleFS.rename(oldName, newName)) {
    server.sendHeader("Location", "/files");
    server.send(303);
  } else {
    server.send(500, "text/plain", "Dosya adı değiştirilemedi");
  }
}
