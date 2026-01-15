#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID "TMPL6hVp-dAoJ"
#define BLYNK_TEMPLATE_NAME "Smart Belt"
#define BLYNK_AUTH_TOKEN "fFVKcA96zKmekF19wPihQ2zLsyAWt7bS"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <BlynkSimpleEsp32.h>
#include <TinyGPS++.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

// --- KONEKSI ---
char ssid[] = "bayar";
char pass[] = "ambilaja";

String botToken = "8440863998:AAFPuBSVdIKKtII_BLtNVNDUnHvQPqUctBE";
String chatID   = "1131481244";

// --- PIN DEFINITION ---
#define BUTTON_PIN 12

#define M_KIRI_PIN   25
#define LED_PIN      27
#define M_KANAN_PIN  26

#define TRIG_KIRI    5
#define ECHO_KIRI    18
#define TRIG_TENGAH  13
#define ECHO_TENGAH  14
#define TRIG_KANAN   19
#define ECHO_KANAN   4 

#define RXD2 16
#define TXD2 17
HardwareSerial gpsSerial(2);
TinyGPSPlus gps;

Adafruit_MPU6050 mpu;
BlynkTimer timer;

// ================= KONFIGURASI =================
#define DIST_NOISE    5    
#define DIST_BAHAYA   40   
#define DIST_MAX      200  

#define PWM_FULL      255  
#define PWM_LOW       60   
#define MOTOR_ACTIVE_LOW 0 

#define ANGLE_WARNING 30.0
#define ANGLE_DANGER  60.0  
#define FALL_RESET    20.0  
#define FALL_COOLDOWN 15000 

#define COLOR_GREEN   "#00FF00"
#define COLOR_YELLOW  "#FFD700"
#define COLOR_RED     "#FF0000"

// ================= DATA =================
float distKiri = 0, distTengah = 0, distKanan = 0;
float pitchAngle = 0;
bool isFalling = false; 
unsigned long lastFallTime = 0;
bool waitingForGPS = false; 
unsigned long lastGpsReportTime = 0; 
String statusString = "INIT";
String statusColor = COLOR_GREEN;

// --- VARIABEL PENGAMAN ---
// Ini untuk mencatat waktu nyala agar 5 detik pertama tidak kirim SOS hantu
unsigned long bootTime = 0; 

// --- TELEGRAM SENDER ---
void sendTelegramMessage(String text) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    text.replace(" ", "%20");
    text.replace("\n", "%0A");
    String url = "https://api.telegram.org/bot" + botToken +
                 "/sendMessage?chat_id=" + chatID + "&text=" + text;
    http.begin(client, url);
    int httpCode = http.GET();
    if(httpCode > 0) Serial.println(">> Telegram Terkirim!");
    else Serial.printf(">> Gagal Telegram: %s\n", http.errorToString(httpCode).c_str());
    http.end();
  }
}

// --- FUNGSI TRIGGER TELEGRAM ---
void triggerTelegram(String headerMsg, bool needGPS) {
  String msg = headerMsg;

  if (needGPS) {
    if (gps.location.isValid()) {
      String link = "http://maps.google.com/?q=" + String(gps.location.lat(), 6) + "," + String(gps.location.lng(), 6);
      msg += "\n\n📍 LOKASI TERKINI:\n" + link;
      msg += "\n(Satelit: " + String(gps.satellites.value()) + ")";
      waitingForGPS = false; 
    } 
    else {
      msg += "\n\n⚠️ STATUS LOKASI: SEDANG DIPROSES...";
      msg += "\n(Sinyal GPS belum terkunci. Lokasi akan dikirim otomatis menyusul)";
      waitingForGPS = true; 
      lastGpsReportTime = millis();
      Serial.println(">>> SOS DITEKAN - MENUNGGU GPS... <<<");
    }
  } else {
    msg += "\n\n(Lokasi tidak disertakan. Tekan tombol SOS untuk meminta lokasi)";
  }
  msg += "\n\nℹ️ Sudut: " + String(pitchAngle, 1) + "°";
  sendTelegramMessage(msg);
}

// --- FUNGSI SUSULAN GPS ---
void cekAntrianGPS() {
  if (waitingForGPS) {
    if (gps.location.isValid()) {
      Serial.println(">>> GPS LOCK! KIRIM SUSULAN... <<<");
      String link = "http://maps.google.com/?q=" + String(gps.location.lat(), 6) + "," + String(gps.location.lng(), 6);
      String msg = "📍 HASIL PROSES LOKASI (SOS):\n";
      msg += link;
      sendTelegramMessage(msg);
      waitingForGPS = false; 
    }
    else {
      if (millis() - lastGpsReportTime > 60000) {
        lastGpsReportTime = millis(); 
        int sat = gps.satellites.value();
        String msg = "⏳ UPDATE PROSES GPS:\nMasih mencari sinyal satelit...\nJumlah Satelit: " + String(sat);
        sendTelegramMessage(msg);
      }
    }
  }
}

// --- BACA SENSOR JARAK ---
float bacaJarak(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000); 
  if (duration == 0) return 400;     
  return duration * 0.034f / 2.0f;   
}

// --- LOGIKA OUTPUT ---
void kontrolOutput(int pin, float jarak) {
  int pwmVal = 0;
  if (jarak < DIST_NOISE) pwmVal = 0; 
  else if (jarak <= DIST_BAHAYA) pwmVal = PWM_FULL; 
  else if (jarak <= DIST_MAX) pwmVal = map((long)jarak, DIST_BAHAYA + 1, DIST_MAX, PWM_FULL, PWM_LOW);
  else pwmVal = 0;
  analogWrite(pin, pwmVal);
}

// --- LOGIKA UTAMA ---
void cekHardware() {
  // A. SENSOR JARAK
  distKiri   = bacaJarak(TRIG_KIRI, ECHO_KIRI); delay(5);
  distTengah = bacaJarak(TRIG_TENGAH, ECHO_TENGAH); delay(5);
  distKanan  = bacaJarak(TRIG_KANAN, ECHO_KANAN);

  // B. OUTPUT
  kontrolOutput(M_KIRI_PIN, distKiri);    
  kontrolOutput(LED_PIN, distTengah);     
  kontrolOutput(M_KANAN_PIN, distKanan);  

  // C. MPU
  sensors_event_t a, g, temp;
  if (mpu.getEvent(&a, &g, &temp)) {
    float tilt = atan2(sqrt(a.acceleration.x*a.acceleration.x + a.acceleration.y*a.acceleration.y), a.acceleration.z) * 180.0 / PI;
    pitchAngle = abs(tilt); 

    if (pitchAngle > ANGLE_DANGER) { 
      statusString = "BAHAYA (POSISI TEGAK/JATUH?)"; 
      statusColor = COLOR_RED;
      
      if (!isFalling) {
        // SAFETY: Jangan kirim alert jatuh kalau baru nyala < 5 detik
        if (millis() - bootTime > 5000) {
            isFalling = true; 
            Serial.println(">>> DETEKSI JATUH <<<");
            triggerTelegram("🚨 PERINGATAN DARURAT!\nSudut Kemiringan Ekstrim Terdeteksi.", false);
            lastFallTime = millis();
        }
      } else {
        if (millis() - lastFallTime > FALL_COOLDOWN) {
          triggerTelegram("⚠️ STATUS: PENGGUNA MASIH DALAM POSISI BAHAYA.", false);
          lastFallTime = millis(); 
        }
      }
    } 
    else if (pitchAngle < FALL_RESET) { 
      statusString = "AMAN (DATAR)";
      statusColor = COLOR_GREEN;
      if (isFalling) {
        Serial.println(">>> KEMBALI AMAN <<<");
        triggerTelegram("✅ INFO PEMULIHAN:\nPosisi sudah kembali aman (Datar).", false);
        isFalling = false; 
      }
    } 
    else {
      statusString = "WASPADA (MIRING)";
      statusColor = COLOR_YELLOW;
    }
  }
}

// --- BLYNK UPDATE & MONITOR (1 DETIK) ---
void updateBlynk() {
  int sat = gps.satellites.value();

  // SERIAL MONITOR
  Serial.println("\n--- [MONITOR DATA 1s] ---");
  Serial.printf("Kiri: %.1f cm | Tengah: %.1f cm | Kanan: %.1f cm\n", distKiri, distTengah, distKanan);
  Serial.printf("Pitch: %.1f deg | Status: %s\n", pitchAngle, statusString.c_str());
  Serial.printf("GPS Satelit: %d\n", sat);
  Serial.println("-------------------------");

  // BLYNK APP
  if (Blynk.connected()) {
    Blynk.virtualWrite(V1, distTengah); 
    Blynk.virtualWrite(V2, distKiri);   
    Blynk.virtualWrite(V3, distKanan);  
    Blynk.virtualWrite(V4, pitchAngle);
    Blynk.virtualWrite(V5, statusString);
    Blynk.setProperty(V5, "color", statusColor);
    Blynk.virtualWrite(V6, sat); 
  }
}

// Reset tombol saat connect
BLYNK_CONNECTED() {
  Blynk.virtualWrite(V0, 0); 
  Serial.println(">> Blynk Connected. Sync Button OFF.");
}

// --- SOS BLYNK (PENGAMAN) ---
BLYNK_WRITE(V0) {
  int value = param.asInt();

  // === FITUR PENGAMAN STARTUP ===
  // Jika alat baru nyala kurang dari 5 detik, abaikan perintah tombol ini.
  // Ini mencegah Telegram terkirim otomatis karena "Sync" dari server.
  if (millis() - bootTime < 5000) {
    if (value == 1) {
      Serial.println("⚠️ Mengabaikan sinyal SOS (Safety Startup 5 detik).");
      // Paksa tombol mati lagi di aplikasi
      Blynk.virtualWrite(V0, 0);
    }
    return;
  }
  // ==============================

  if (value == 1) {
    Serial.println(">> Tombol SOS App Ditekan!");
    triggerTelegram("🚨 SOS MANUAL (APP)!", true);
  }
}

// --- SOS FISIK ---
void cekTombolFisik() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      // Tombol fisik juga kita beri safety startup 2 detik biar ga false trigger
      if (millis() - bootTime > 2000) { 
        if (Blynk.connected()) Blynk.virtualWrite(V0, 1);
        triggerTelegram("🚨 SOS MANUAL (TOMBOL FISIK)!", true);
        while (digitalRead(BUTTON_PIN) == LOW) delay(10);
        if (Blynk.connected()) Blynk.virtualWrite(V0, 0);
      }
    }
  }
}

void setup() {
  bootTime = millis(); // CATAT WAKTU NYALA
  
  Serial.begin(115200);
  Serial.println("\n\n=== BOOTING SMART BELT (SAFETY START) ===");

  pinMode(TRIG_KIRI, OUTPUT);   pinMode(ECHO_KIRI, INPUT);
  pinMode(TRIG_TENGAH, OUTPUT); pinMode(ECHO_TENGAH, INPUT);
  pinMode(TRIG_KANAN, OUTPUT);  pinMode(ECHO_KANAN, INPUT); 

  pinMode(M_KIRI_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);    
  pinMode(M_KANAN_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(21, 22); 
  Wire.setClock(100000); 
  
  if (!mpu.begin()) {
    Serial.println("❌ MPU ERROR! Cek Kabel SDA/SCL.");
    delay(1000);
    if(mpu.begin()) Serial.println("✅ MPU Recovered!");
  }
  else {
    Serial.println("✅ MPU OK!");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  gpsSerial.begin(9600, SERIAL_8N1, RXD2, TXD2);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500); Serial.print("."); timeout++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect();
    Serial.println("✅ Online!");
  }

  timer.setInterval(100L, cekHardware);    
  timer.setInterval(1000L, updateBlynk);   
  timer.setInterval(200L, cekTombolFisik); 
  timer.setInterval(2000L, cekAntrianGPS); 
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) Blynk.run();
  timer.run();
  while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
}