/**
 * ESP32 AI Voice-Activated Alert Tone Generator - FINAL FIRMWARE
 * ===============================================================
 * Backend URL: Cloudflare Workers (https://empty-rain-137d.ahmedsamy200525.workers.dev)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <math.h>
#include <stdlib.h>

// ==================== Configuration ====================
const char* WIFI_SSID = "ELSAKY";
const char* WIFI_PASS = "23010111";

// ✅ تم تحديث الرابط للرابط السحابي الجديد (تأكد من وجود https://)
const char* HOST = "https://empty-rain-137d.ahmedsamy200525.workers.dev";

#define USE_VOICE true
const char* TEXT_PROMPT = "";

// I2S Pin Configuration
#define MIC_SCK  32
#define MIC_WS   25
#define MIC_SD   33
#define SPK_BCLK 27
#define SPK_LRC  26
#define SPK_DOUT 22

// Button pin (D13)
#define BUTTON_PIN 13

// Audio settings
#define SR_PLAY   22050
#define SR_REC    16000
#define REC_SEC   2
#define VOLUME_THRESHOLD 300 // فلتر الصوت: يتجاهل التسجيل لو الصوت أوطى من كده

// ==================== Data Structures ====================
struct Tone {
  float frequency;
  uint32_t duration_ms;
};

Tone tones[10];
int toneCount = 0;

// ==================== Audio Recording ====================
int recordWav(uint8_t** outWav, size_t* outSize, int recSec) {
  const int total_samples = SR_REC * recSec;
  size_t data_size = total_samples * 2;
  size_t ws = 44 + data_size;

  uint8_t* wav = (uint8_t*)malloc(ws);
  if (!wav) return -1;

  int32_t raw; size_t br;
  for (int i = 0; i < 512; i++) i2s_read(I2S_NUM_0, &raw, 4, &br, portMAX_DELAY);

  int16_t* pcm_ptr = (int16_t*)(wav + 44);
  long long sumVolume = 0;
  
  for (int i = 0; i < total_samples; i++) {
    i2s_read(I2S_NUM_0, &raw, 4, &br, portMAX_DELAY);
    int16_t sample = (int16_t)(raw >> 16);
    pcm_ptr[i] = sample;
    
    int16_t val = sample;
    if (val < 0) val = -val;
    sumVolume += val;
  }

  int avgVolume = sumVolume / total_samples;

  // Write WAV Header
  memcpy(wav, "RIFF", 4);
  uint32_t v32 = ws - 8;  memcpy(wav + 4, &v32, 4);
  memcpy(wav + 8, "WAVEfmt ", 8);
  v32 = 16;               memcpy(wav + 16, &v32, 4);
  uint16_t v16 = 1;       memcpy(wav + 20, &v16, 2);
  v16 = 1;                memcpy(wav + 22, &v16, 2);
  v32 = SR_REC;           memcpy(wav + 24, &v32, 4);
  v32 = SR_REC * 2;       memcpy(wav + 28, &v32, 4);
  v16 = 2;                memcpy(wav + 32, &v16, 2);
  v16 = 16;               memcpy(wav + 34, &v16, 2);
  memcpy(wav + 36, "data", 4);
  v32 = data_size;        memcpy(wav + 40, &v32, 4);

  *outWav = wav;
  *outSize = ws;
  return avgVolume;
}

// ==================== Backend Communication ====================
String askBackend(uint8_t* wav, size_t wavSize) {
  HTTPClient http;
  String payload = "";
  int code = -1;
  http.setTimeout(45000);

  if (USE_VOICE && wav) {
    http.begin(String(HOST) + "/generate-melody-voice");
    http.addHeader("Content-Type", "audio/wav");
    code = http.POST(wav, wavSize);
  } else {
    http.begin(String(HOST) + "/generate-melody");
    http.addHeader("Content-Type", "application/json");
    code = http.POST(String("{\"prompt\":\"") + TEXT_PROMPT + "\"}");
  }

  Serial.printf("[HTTP] code: %d\n", code);
  if (code == 200) payload = http.getString();
  else Serial.println("[HTTP] FAIL: " + http.errorToString(code));
  http.end();
  return payload;
}

// ==================== Tone Processing & Synthesis ====================
void buildTones(JsonDocument& doc) {
  toneCount = 0;
  JsonArray t = doc["tones"];
  for (JsonVariant v : t) {
    if (toneCount >= 10) break;
    tones[toneCount].frequency = v["frequency"] | 440.0;
    tones[toneCount].duration_ms = v["duration_ms"] | 200;
    toneCount++;
  }
}

void playTone(float freq, uint32_t duration_ms) {
  const int samples = (SR_PLAY * duration_ms) / 1000;
  const int phase_step = (int)((freq * 1024) / SR_PLAY);
  int phase = 0;

  for (int i = 0; i < samples; i++) {
    float sample = sin(phase * 2 * PI / 1024.0);
    int16_t out = (int16_t)(sample * 32000.0);
    size_t wr;
    i2s_write(I2S_NUM_1, &out, 2, &wr, portMAX_DELAY);
    phase += phase_step;
    if (phase >= 1024) phase -= 1024;
  }
}

void playAllTones() {
  for (int i = 0; i < toneCount; i++) {
    playTone(tones[i].frequency, tones[i].duration_ms);
    delay(50);
  }
}

// ==================== I2S Configuration ====================
void initI2S() {
  // Microphone (RIGHT channel)
  i2s_config_t rx = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SR_REC,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, 
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_driver_install(I2S_NUM_0, &rx, 0, NULL);
  i2s_pin_config_t rp = { .bck_io_num = MIC_SCK, .ws_io_num = MIC_WS,
                          .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = MIC_SD };
  i2s_set_pin(I2S_NUM_0, &rp);

  // Speaker (LEFT channel)
  i2s_config_t tx = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SR_PLAY,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_driver_install(I2S_NUM_1, &tx, 0, NULL);
  i2s_pin_config_t tp = { .bck_io_num = SPK_BCLK, .ws_io_num = SPK_LRC,
                          .data_out_num = SPK_DOUT, .data_in_num = I2S_PIN_NO_CHANGE };
  i2s_set_pin(I2S_NUM_1, &tp);
}

// ==================== Setup & Loop ====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 Alert Tone Generator (Cloudflare) ===");
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) { delay(500); Serial.print("."); tries++; }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("\nWiFi FAILED! code: %d\n", (int)WiFi.status());
    while (true) delay(1000);
  }
  Serial.println(" OK | IP: " + WiFi.localIP().toString());
  initI2S();
  Serial.println("I2S ready");
  Serial.println("\nPress and RELEASE the button to record for 2 seconds!");
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      while (digitalRead(BUTTON_PIN) == LOW) { delay(10); }
      
      Serial.println("\n[BUTTON] Released. Recording for 2 seconds...");
      playTone(440, 100);
      
      uint8_t* wav = nullptr; size_t ws = 0;
      int volume = recordWav(&wav, &ws, REC_SEC);
      
      Serial.printf("[MIC] Volume Level: %d\n", volume);
      
      if (volume < VOLUME_THRESHOLD) {
        Serial.println("[MIC] Too quiet! Ignoring silence/noise.");
        if (wav) free(wav);
        delay(500);
        return;
      }
      
      Serial.printf("[MIC] Good volume! Recorded %d bytes\n", ws);

      String payload = askBackend(wav, ws);
      if (wav) free(wav);
      if (payload.length() == 0) { delay(1000); return; }

      JsonDocument doc;
      if (deserializeJson(doc, payload)) { Serial.println("JSON fail"); delay(1000); return; }

      buildTones(doc);
      Serial.printf("[TONES] %s | count=%d\n", (const char*)doc["description"], toneCount);
      Serial.println("[PLAY] ...");
      playAllTones();
      Serial.println("[DONE]");
      
      delay(500);
    }
  }
  delay(10);
}
