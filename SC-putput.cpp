#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <driver/i2s.h>

const char* ssid = "Asli 1";
const char* password = "bolahijao";

// ================= PENGATURAN IP STATIS (TRIK 2) =================
// Kita kunci IP ESP2 ini di akhiran .100 agar mudah diingat
IPAddress local_IP(192, 168, 1, 100); 
IPAddress gateway(192, 168, 1, 1);    // IP pusat router/hotspot
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);      // DNS Google (WAJIB agar Telegram bisa jalan)

// ================= TELEGRAM =================
#define BOTtoken "8533600443:AAHO4P9iVHnM1eVUw16_-qwUUa7Zc7XsAmM"
#define CHAT_ID "1439709120"
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

// ================= UDP =================
const int udpPortAudio = 3333; 
const int udpPortCmd   = 3334; 
WiFiUDP udpAudio;
WiFiUDP udpCmd;

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

int currentBPM = 0;
String oledStatus = "STANDBY";

// ================= SPEAKER I2S =================
#define I2S_LRC_SPK 25
#define I2S_BCLK_SPK 26
#define I2S_DIN_SPK 27 // WAJIB PIN 27

uint8_t audioBuffer[512];
unsigned long lastPacketTime = 0;
bool isSpeakerActive = false;

void setupSpeaker() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 8000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 128,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK_SPK,
    .ws_io_num = I2S_LRC_SPK,
    .data_out_num = I2S_DIN_SPK,
    .data_in_num = -1
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("=== KURSI RODA ===");
  display.drawLine(0, 10, 128, 10, SH110X_WHITE);

  display.setCursor(0, 20);
  display.print("HEART RATE: ");
  display.setCursor(0, 32);
  display.setTextSize(3);
  if (currentBPM == 0) display.print("--");
  else display.print(currentBPM);
  display.setTextSize(1);
  display.print(" BPM");

  display.setCursor(0, 56);
  display.setTextSize(1);
  display.println(oledStatus);
  display.display();
}

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);
  delay(250);
  display.begin(0x3C, true);
  
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  // --- EKSEKUSI PENGUNCIAN IP ---
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    Serial.println("Gagal mengunci IP Statis!");
  }
  // ------------------------------

  WiFi.begin(ssid, password);
  
  Serial.print("Menyambung ke WiFi");
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  
  Serial.println("\nESP2 READY!");
  Serial.print("IP ESP2 Terkunci Di: ");
  Serial.println(WiFi.localIP()); // Pastikan di Serial Monitor angkanya 192.168.43.100

  client.setInsecure(); 
  setupSpeaker();
  udpAudio.begin(udpPortAudio);
  udpCmd.begin(udpPortCmd);
  
  updateOLED();
}

void loop() {
  // ================= 1. TERIMA PERINTAH (BPM / MIC) =================
  int cmdSize = udpCmd.parsePacket();
  if (cmdSize > 0) {
    char cmdBuffer[50];
    int len = udpCmd.read(cmdBuffer, sizeof(cmdBuffer) - 1);
    if (len > 0) {
      cmdBuffer[len] = '\0';
      String command = String(cmdBuffer);

      if (command.startsWith("BPM:")) {
        int receivedBPM = command.substring(4).toInt();
        if (receivedBPM != currentBPM) {
          currentBPM = receivedBPM;
          if (oledStatus != "MEMAINKAN SUARA...") {
            oledStatus = (currentBPM > 0) ? "MENGUKUR..." : "STANDBY";
            updateOLED();
          }
        }
      } 
      else if (command == "MIC_START") {
        oledStatus = "KIRIM TELEGRAM...";
        updateOLED();
        
        bot.sendMessage(CHAT_ID, "🚨 PERINGATAN DARURAT\n\nPengguna meminta bantuan!\n🎤 Suara aktif 20 detik.", "");
        
        // Bersihkan sisa memori sebelum mainkan suara
        i2s_zero_dma_buffer(I2S_NUM_0); 
        oledStatus = "MEMAINKAN SUARA...";
        updateOLED();
      } 
      else if (command == "MIC_STOP") {
        oledStatus = "STANDBY";
        updateOLED();
        bot.sendMessage(CHAT_ID, "✅ Suara darurat selesai.", "");
      }
    }
  }

  // ================= 2. TERIMA SUARA =================
  int audioSize = udpAudio.parsePacket();
  if (audioSize > 0) {
    
    lastPacketTime = millis();
    isSpeakerActive = true;
    
    int len = udpAudio.read(audioBuffer, sizeof(audioBuffer));
    
    if (len > 0) {
      int16_t* sampleBuffer = (int16_t*)audioBuffer;

      for (int i = 0; i < len / 2; i++) {
        int32_t sample = sampleBuffer[i];
        sample = sample * 2; 
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        sampleBuffer[i] = sample;
      }

      size_t bytesWritten;
      i2s_write(I2S_NUM_0, audioBuffer, len, &bytesWritten, portMAX_DELAY);
    }
  } 
  else {
    // FITUR AUTO-MUTE
    if (isSpeakerActive && (millis() - lastPacketTime > 100)) {
      i2s_zero_dma_buffer(I2S_NUM_0); 
      isSpeakerActive = false;        
    }
  }
}
