/**
 * ESP32 AI Voice-Activated Alert Tone Generator - Firmware
 * =========================================================
 * 
 * This firmware runs on ESP32 DevKit V1 and handles:
 * - I2S audio input from INMP441 microphone
 * - I2S audio output to MAX98357A amplifier
 * - WiFi connectivity for backend communication
 * - Real-time audio synthesis using sine wave generation
 * - Voice Activity Detection (VAD) to filter background noise
 * 
 * Hardware Configuration:
 * - Microphone: INMP441 (I2S) on GPIO 32, 25, 33
 * - Amplifier: MAX98357A (I2S) on GPIO 27, 26, 22
 * - Button: GPIO 13 (with internal pull-up)
 * 
 * Author: Ahmed
 * Date: October 2026
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <math.h>
#include <stdlib.h>

// ==================== Configuration ====================

// WiFi credentials
const char* WIFI_SSID = "ELSAKY";
const char* WIFI_PASS = "23010111";

// Backend server URL
const char* HOST = "http://192.168.1.11:8000";

// Voice recording mode
#define USE_VOICE true
const char* TEXT_PROMPT = "";

// I2S Pin Configuration
// Microphone (INMP441)
#define MIC_SCK  32  // Serial Clock
#define MIC_WS   25  // Word Select
#define MIC_SD   33  // Serial Data

// Amplifier (MAX98357A)
#define SPK_BCLK 27  // Bit Clock
#define SPK_LRC  26  // Left-Right Clock
#define SPK_DOUT 22  // Digital Output

// Button pin (with internal pull-up resistor)
#define BUTTON_PIN 13

// Audio sampling rates
#define SR_PLAY   22050  // Playback sample rate (Hz)
#define SR_REC    16000  // Recording sample rate (Hz)
#define REC_SEC   2      // Recording duration (seconds)

// Voice Activity Detection threshold
// If average volume is below this value, recording is ignored
#define VOLUME_THRESHOLD 300

// ==================== Data Structures ====================

/**
 * Tone structure for storing frequency and duration
 */
struct Tone {
  float frequency;        // Frequency in Hz
  uint32_t duration_ms;   // Duration in milliseconds
};

// Array to store up to 10 tones
Tone tones[10];
int toneCount = 0;

// ==================== Audio Recording ====================

/**
 * Record audio from I2S microphone and save as WAV file.
 * Also calculates average volume for Voice Activity Detection.
 * 
 * @param outWav Pointer to store WAV data pointer
 * @param outSize Pointer to store WAV file size
 * @param recSec Recording duration in seconds
 * @return Average volume level (0-32767)
 */
int recordWav(uint8_t** outWav, size_t* outSize, int recSec) {
  const int total_samples = SR_REC * recSec;
  size_t data_size = total_samples * 2;  // 16-bit samples
  size_t ws = 44 + data_size;  // WAV header (44 bytes) + data

  // Allocate memory for WAV file
  uint8_t* wav = (uint8_t*)malloc(ws);
  if (!wav) return -1;

  int32_t raw;
  size_t br;
  
  // Flush I2S buffer
  for (int i = 0; i < 512; i++) {
    i2s_read(I2S_NUM_0, &raw, 4, &br, portMAX_DELAY);
  }

  // Read audio samples and calculate volume
  int16_t* pcm_ptr = (int16_t*)(wav + 44);
  long long sumVolume = 0;
  
  for (int i = 0; i < total_samples; i++) {
    i2s_read(I2S_NUM_0, &raw, 4, &br, portMAX_DELAY);
    int16_t sample = (int16_t)(raw >> 16);
    pcm_ptr[i] = sample;
    
    // Calculate absolute value for volume measurement
    int16_t val = sample;
    if (val < 0) val = -val;
    sumVolume += val;
  }

  // Calculate average volume
  int avgVolume = sumVolume / total_samples;

  // Write WAV header
  memcpy(wav, "RIFF", 4);
  uint32_t v32 = ws - 8;
  memcpy(wav + 4, &v32, 4);
  memcpy(wav + 8, "WAVEfmt ", 8);
  v32 = 16;
  memcpy(wav + 16, &v32, 4);
  uint16_t v16 = 1;
  memcpy(wav + 20, &v16, 2);
  v16 = 1;
  memcpy(wav + 22, &v16, 2);
  v32 = SR_REC;
  memcpy(wav + 24, &v32, 4);
  v32 = SR_REC * 2;
  memcpy(wav + 28, &v32, 4);
  v16 = 2;
  memcpy(wav + 32, &v16, 2);
  v16 = 16;
  memcpy(wav + 34, &v16, 2);
  memcpy(wav + 36, "data", 4);
  v32 = data_size;
  memcpy(wav + 40, &v32, 4);

  *outWav = wav;
  *outSize = ws;
  return avgVolume;
}

// ==================== Backend Communication ====================

/**
 * Send audio data to backend server and receive JSON response.
 * 
 * @param wav WAV audio data
 * @param wavSize Size of WAV data
 * @return JSON response string from backend
 */
String askBackend(uint8_t* wav, size_t wavSize) {
  HTTPClient http;
  String payload = "";
  int code = -1;
  http.setTimeout(45000);  // 45 second timeout

  if (USE_VOICE && wav) {
    // Send voice recording to backend
    http.begin(String(HOST) + "/generate-melody-voice");
    http.addHeader("Content-Type", "audio/wav");
    code = http.POST(wav, wavSize);
  } else {
    // Send text prompt to backend
    http.begin(String(HOST) + "/generate-melody");
    http.addHeader("Content-Type", "application/json");
    code = http.POST(String("{\"prompt\":\"") + TEXT_PROMPT + "\"}");
  }

  Serial.printf("[HTTP] code: %d\n", code);
  if (code == 200) {
    payload = http.getString();
  } else {
    Serial.println("[HTTP] FAIL: " + http.errorToString(code));
  }
  http.end();
  return payload;
}

// ==================== Tone Processing ====================

/**
 * Parse JSON response and extract tone parameters.
 * 
 * @param doc ArduinoJson document containing backend response
 */
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

// ==================== Audio Synthesis ====================

/**
 * Generate and play a single sine wave tone.
 * Uses phase accumulator for accurate frequency generation.
 * 
 * @param freq Frequency in Hz
 * @param duration_ms Duration in milliseconds
 */
void playTone(float freq, uint32_t duration_ms) {
  const int samples = (SR_PLAY * duration_ms) / 1000;
  const int phase_step = (int)((freq * 1024) / SR_PLAY);
  int phase = 0;

  for (int i = 0; i < samples; i++) {
    // Generate sine wave using phase accumulator
    float sample = sin(phase * 2 * PI / 1024.0);
    int16_t out = (int16_t)(sample * 32000.0);
    size_t wr;
    i2s_write(I2S_NUM_1, &out, 2, &wr, portMAX_DELAY);
    phase += phase_step;
    if (phase >= 1024) phase -= 1024;
  }
}

/**
 * Play all tones in sequence with 50ms gap between each.
 */
void playAllTones() {
  for (int i = 0; i < toneCount; i++) {
    playTone(tones[i].frequency, tones[i].duration_ms);
    delay(50);  // Gap between tones
  }
}

// ==================== I2S Configuration ====================

/**
 * Initialize I2S interfaces for microphone (input) and amplifier (output).
 * Microphone uses RIGHT channel, amplifier uses LEFT channel.
 */
void initI2S() {
  // Configure microphone input (I2S_NUM_0)
  i2s_config_t rx = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SR_REC,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,  // INMP441 on RIGHT channel
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_driver_install(I2S_NUM_0, &rx, 0, NULL);
  i2s_pin_config_t rp = {
    .bck_io_num = MIC_SCK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };
  i2s_set_pin(I2S_NUM_0, &rp);

  // Configure amplifier output (I2S_NUM_1)
  i2s_config_t tx = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SR_PLAY,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // MAX98357A on LEFT channel
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_driver_install(I2S_NUM_1, &tx, 0, NULL);
  i2s_pin_config_t tp = {
    .bck_io_num = SPK_BCLK,
    .ws_io_num = SPK_LRC,
    .data_out_num = SPK_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_set_pin(I2S_NUM_1, &tp);
}

// ==================== Setup & Loop ====================

/**
 * Initialize hardware and establish WiFi connection.
 */
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 Alert Tone Generator ===");
  
  // Configure button pin with internal pull-up resistor
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("\nWiFi FAILED! code: %d\n", (int)WiFi.status());
    while (true) delay(1000);
  }
  Serial.println(" OK | IP: " + WiFi.localIP().toString());
  
  // Initialize I2S interfaces
  initI2S();
  Serial.println("I2S ready");
  Serial.println("\nPress and RELEASE the button to record for 2 seconds!");
}

/**
 * Main loop: Wait for button press, record audio, send to backend, play tones.
 */
void loop() {
  // Check if button is pressed (active LOW)
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);  // Debounce
    if (digitalRead(BUTTON_PIN) == LOW) {
      // Wait for button release
      while (digitalRead(BUTTON_PIN) == LOW) {
        delay(10);
      }
      
      Serial.println("\n[BUTTON] Released. Recording for 2 seconds...");
      playTone(440, 100);  // Beep to indicate recording started
      
      // Record audio
      uint8_t* wav = nullptr;
      size_t ws = 0;
      int volume = recordWav(&wav, &ws, REC_SEC);
      
      Serial.printf("[MIC] Volume Level: %d\n", volume);
      
      // Voice Activity Detection: Ignore if too quiet
      if (volume < VOLUME_THRESHOLD) {
        Serial.println("[MIC] Too quiet! Ignoring silence/noise.");
        if (wav) free(wav);
        delay(500);
        return;
      }
      
      Serial.printf("[MIC] Good volume! Recorded %d bytes\n", ws);

      // Send to backend and get response
      String payload = askBackend(wav, ws);
      if (wav) free(wav);
      if (payload.length() == 0) {
        delay(1000);
        return;
      }

      // Parse JSON response
      JsonDocument doc;
      if (deserializeJson(doc, payload)) {
        Serial.println("JSON fail");
        delay(1000);
        return;
      }

      // Extract and play tones
      buildTones(doc);
      Serial.printf("[TONES] %s | count=%d\n", 
                    (const char*)doc["description"], toneCount);
      Serial.println("[PLAY] ...");
      playAllTones();
      Serial.println("[DONE]");
      
      delay(500);
    }
  }
  delay(10);
}