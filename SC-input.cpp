#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <HTTPClient.h> // WAJIB ADA UNTUK KIRIM KE WEB
#include "MAX30105.h"
#include "heartRate.h"

const char* ssid = "Asli 1";
const char* password = "bolahijao";

// ================= PENGATURAN TUJUAN =================
const char* udpAddress = "192.168.1.100"; 
const int udpPortAudio = 3333; 
const int udpPortCmd   = 3334; 
WiFiUDP udp;

// URL Web Laravel
String serverBPM = "http://192.168.1.17:8000/api/heart-rate";

// ================= SENSOR JANTUNG =================
MAX30105 particleSensor;
const byte RATE_SIZE = 8; 
byte rates[RATE_SIZE]; 
byte rateSpot = 0;
long lastBeat = 0; 
int beatAvg = 0;
unsigned long lastBpmSendTime = 0;

// ================= MIC & TOMBOL =================
#define I2S_SCK_MIC 13
#define I2S_WS_MIC 14
#define I2S_SD_MIC 32
#define BUTTON_PIN 4

bool isMicActive = false;
int lastButtonState = HIGH;
unsigned long micStartTime = 0;
const unsigned long micDuration = 20000; 

void setupMic() {
  i2s_config_t mic_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 8000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 128,
    .use_apll = false
  };
  i2s_pin_config_t mic_pins = {
    .bck_io_num = I2S_SCK_MIC,
    .ws_io_num = I2S_WS_MIC,
    .data_out_num = -1,
    .data_in_num = I2S_SD_MIC
  };
  i2s_driver_install(I2S_NUM_0, &mic_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &mic_pins);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(21, 22);
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("Sensor Jantung GAGAL!");
    while (1);
  }
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0xFF); 
  particleSensor.setPulseAmplitudeGreen(0);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  Serial.println("\nESP1 (INPUT) READY!");
  Serial.print("IP ESP1: ");
  Serial.println(WiFi.localIP());
  
  setupMic();
}

void sendCommand(String cmd) {
  udp.beginPacket(udpAddress, udpPortCmd);
  udp.print(cmd);
  udp.endPacket();
}

void loop() {
  int currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    sendCommand("MIC_START");
    delay(3000); 
    isMicActive = true;
    micStartTime = millis();
  }
  lastButtonState = currentButtonState;

  if (isMicActive && (millis() - micStartTime >= micDuration)) {
    isMicActive = false;
    sendCommand("MIC_STOP");
  }

  if (isMicActive) {
    int32_t rawBuffer[128];
    int16_t audioBuffer[128];
    size_t bytesRead;
    i2s_read(I2S_NUM_0, rawBuffer, sizeof(rawBuffer), &bytesRead, portMAX_DELAY);
    
    int samplesRead = bytesRead / 4;
    for (int i = 0; i < samplesRead; i++) {
      int32_t sample = rawBuffer[i] >> 13; 
      if (sample > -30 && sample < 30) sample = 0; 
      if (sample > 32767) sample = 32767;          
      if (sample < -32768) sample = -32768;        
      audioBuffer[i] = sample;
    }
    
    udp.beginPacket(udpAddress, udpPortAudio);
    udp.write((uint8_t*)audioBuffer, samplesRead * sizeof(int16_t));
    udp.endPacket();
  } 
  else {
    long irValue = particleSensor.getIR();
    
    if (irValue > 15000) {
      if (checkForBeat(irValue) == true) {
        long delta = millis() - lastBeat;
        lastBeat = millis();
        float beatsPerMinute = 60 / (delta / 1000.0);
        
        if (beatsPerMinute > 40 && beatsPerMinute < 180) {
          rates[rateSpot++] = (byte)beatsPerMinute; 
          rateSpot %= RATE_SIZE; 
          beatAvg = 0;
          for (byte x = 0 ; x < RATE_SIZE ; x++) beatAvg += rates[x];
          beatAvg /= RATE_SIZE;
        }
      }
    } else {
      beatAvg = 0; 
      for (byte x = 0 ; x < RATE_SIZE ; x++) rates[x] = 0;
    }

    // === PENGIRIMAN DATA GANDA (KE ESP2 DAN KE LARAVEL) ===
    if (millis() - lastBpmSendTime > 1000) {
      if(WiFi.status() == WL_CONNECTED) {
        // 1. Kirim UDP ke ESP2 (Layar OLED)
        sendCommand("BPM:" + String(beatAvg));

        // 2. Kirim HTTP POST ke Laravel Dashboard
        if (beatAvg > 0) {
          HTTPClient http;
          http.begin(serverBPM);
          http.addHeader("Content-Type", "application/x-www-form-urlencoded");
          String postData = "bpm=" + String(beatAvg);
          
          int httpResponseCode = http.POST(postData);
          http.end();
          
          Serial.print("Data BPM dikirim ke Laravel: ");
          Serial.println(beatAvg);
        }
      }
      lastBpmSendTime = millis();
    }
  }
}
