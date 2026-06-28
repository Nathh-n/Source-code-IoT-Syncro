#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <WiFi.h>        
#include <HTTPClient.h>  

// ================= PENGATURAN WIFI & URL =================
const char* ssid = "Asli 1";
const char* password = "bolahijao";

// 🌐 URL API LARAVEL KAMU (Saya ganti ke link Ngrok-mu agar aman dari blokir)
// Pastikan link ini adalah link Ngrok yang aktif hari ini!
String serverGPS = "http://192.168.1.17:8000/api/gps";

// ================= PIN L298N MOTOR =================
#define ENA 22
#define IN1 16
#define IN2 17
#define IN3 18
#define IN4 19
#define ENB 23

// ================= PIN JOYSTICK =================
#define VRX 34 
#define VRY 35 

// ================= PIN GPS =================
#define RX_PIN 13 
#define TX_PIN 14 

HardwareSerial gpsSerial(2);
TinyGPSPlus gps;

unsigned long lastGpsSendTime = 0;

void maju(); void mundur(); void kiri(); void kanan(); void berhenti(); void bacaGPS();

void setup() {
  Serial.begin(115200);
  
  pinMode(ENA, OUTPUT); pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT); pinMode(ENB, OUTPUT);
  berhenti();

  // Koneksi ke WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Menyambung ke WiFi ESP3");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Terhubung!");

  gpsSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial.println("SISTEM KURSI RODA + GPS SIAP TEMPUR!");
}

void loop() {
  bacaGPS(); // ESP32 mendengarkan satelit setiap saat

  int xVal = analogRead(VRX); 
  int yVal = analogRead(VRY); 

  if (yVal < 1000) maju();
  else if (yVal > 3000) mundur();
  else if (xVal < 1000) kiri();
  else if (xVal > 3000) kanan();
  else berhenti();

  delay(20); 
}

// ================= FUNGSI PEMBACAAN DAN PENGIRIMAN GPS =================
void bacaGPS() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // Jika GPS berhasil mendapatkan lokasi (Lock Satelit)
  if (gps.location.isUpdated()) {
    float lat = gps.location.lat();
    float lng = gps.location.lng();
    
    // Kirim ke Web setiap 3 detik agar gerakan peta stabil
    if (millis() - lastGpsSendTime > 3000) {
      if(WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(serverGPS);
        
        // Header wajib agar data bisa dibaca Laravel & tidak diblokir Ngrok
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");
        http.addHeader("ngrok-skip-browser-warning", "true"); 
        
        // Gabungkan data koordinat
        String postData = "latitude=" + String(lat, 6) + "&longitude=" + String(lng, 6);
        
        int httpResponseCode = http.POST(postData);
        http.end();
        
        // Cetak di layar agar ketahuan berhasil atau gagal
        Serial.print("Kordinat Dikirim ke Web! Lat: ");
        Serial.print(lat, 6);
        Serial.print(" | Status HTTP: ");
        Serial.println(httpResponseCode);
      }
      lastGpsSendTime = millis();
    }
  }
}

// ================= FUNGSI GERAK MOTOR =================
void maju() {
  digitalWrite(ENA, HIGH); digitalWrite(ENB, HIGH); 
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); 
}
void mundur() {
  digitalWrite(ENA, HIGH); digitalWrite(ENB, HIGH);
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}
void kiri() {
  digitalWrite(ENA, HIGH); digitalWrite(ENB, HIGH);
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}
void kanan() {
  digitalWrite(ENA, HIGH); digitalWrite(ENB, HIGH);
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
}
void berhenti() {
  digitalWrite(ENA, LOW); digitalWrite(ENB, LOW);
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}
