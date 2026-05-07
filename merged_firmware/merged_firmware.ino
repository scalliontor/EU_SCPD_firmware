// =============================================================
//  EU-SCPD Merged Firmware - Arduino Mega 2560
//  6x WS2812B LED Strips (số bóng tùy chỉnh) + 6x MAX4466 Mic
// =============================================================
//
//  LED Pins:  D2, D3, D4, D5, D6, D7
//  Mic Pins:  A0, A1, A2, A3, A4, A5
//
//  Serial Protocol (115200 baud):
//  ─────────────────────────────────────────────────
//  Arduino → PC:
//    MIC:p0,p1,p2,p3,p4,p5
//
//  PC → Arduino:
//    LED <0-5|ALL> NUMLEDS <count>           ← Đặt số bóng LED cho dải
//    LED <0-5|ALL> COLOR <r> <g> <b>
//    LED <0-5|ALL> BRIGHTNESS <0-255>
//    LED <0-5|ALL> CLEAR
//    LED <0-5|ALL> MODE <0|1|2>              ← 0=tĩnh, 1=cầu vồng, 2=chase
//    LED <0-5|ALL> PIXEL <index> <r> <g> <b>
//    LED <0-5|ALL> SPEED <10-500>            ← Tốc độ hiệu ứng (ms)
//    MIC PAUSE
//    MIC RESUME
//    MIC RATE <20-500>                       ← Thời gian lấy mẫu mic (ms)
//    STATUS
// =============================================================

#include <Arduino.h>
#include <FastLED.h>

// ─── CẤU HÌNH ───────────────────────────────────────────────
#define MAX_LEDS_PER_STRIP 150   // Buffer tối đa mỗi dải (cấp phát cố định)
#define NUM_STRIPS         6
#define NUM_MICS           6
#define DEFAULT_BRIGHTNESS 100
#define DEFAULT_NUM_LEDS   4     // Số bóng mặc định khi khởi động
#define DEFAULT_FX_SPEED   20    // Tốc độ hiệu ứng mặc định (ms)
#define DEFAULT_MIC_RATE   50    // Chu kỳ mic mặc định (ms)

// Chân Digital cho 6 dải LED
#define LED_PIN_0  2
#define LED_PIN_1  3
#define LED_PIN_2  4
#define LED_PIN_3  5
#define LED_PIN_4  6
#define LED_PIN_5  7

// Chân Analog cho 6 mic
const int micPins[NUM_MICS] = {A0, A1, A2, A3, A4, A5};

// ─── LED DATA ───────────────────────────────────────────────
CRGB leds[NUM_STRIPS][MAX_LEDS_PER_STRIP];

int  activeNumLeds[NUM_STRIPS];              // Số bóng đang dùng (có thể thay đổi)
int  stripMode[NUM_STRIPS];                  // 0=tĩnh, 1=cầu vồng, 2=chase
int  stripSpeed[NUM_STRIPS];                 // Tốc độ hiệu ứng (ms)
unsigned long lastFxUpdate[NUM_STRIPS];      // Timestamp cập nhật hiệu ứng
uint8_t fxState[NUM_STRIPS];                 // Trạng thái nội bộ hiệu ứng

// ─── MIC DATA ───────────────────────────────────────────────
unsigned int signalMax[NUM_MICS];
unsigned int signalMin[NUM_MICS];
unsigned long micSampleStart = 0;
bool micSampling    = false;
bool micEnabled     = true;
int  micSampleRate  = DEFAULT_MIC_RATE;      // Có thể thay đổi runtime

// ─── BRIGHTNESS ─────────────────────────────────────────────
int globalBrightness = DEFAULT_BRIGHTNESS;

// =============================================================
//  SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  // Khởi tạo giá trị mặc định
  for (int i = 0; i < NUM_STRIPS; i++) {
    activeNumLeds[i] = DEFAULT_NUM_LEDS;
    stripMode[i]     = 0;
    stripSpeed[i]    = DEFAULT_FX_SPEED;
    lastFxUpdate[i]  = 0;
    fxState[i]       = 0;
  }

  // FastLED: đăng ký 6 dải với buffer tối đa
  // Số bóng thực tế được kiểm soát bằng activeNumLeds[]
  FastLED.addLeds<WS2812B, LED_PIN_0, GRB>(leds[0], MAX_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, LED_PIN_1, GRB>(leds[1], MAX_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, LED_PIN_2, GRB>(leds[2], MAX_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, LED_PIN_3, GRB>(leds[3], MAX_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, LED_PIN_4, GRB>(leds[4], MAX_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, LED_PIN_5, GRB>(leds[5], MAX_LEDS_PER_STRIP);

  FastLED.setBrightness(globalBrightness);
  FastLED.clear(true);

  Serial.println("READY EU-SCPD 6LED+6MIC v2");
}

// =============================================================
//  LED COMMAND: áp dụng cho 1 dải
// =============================================================
void applyLedCommand(int s, String action, String params) {
  action.toUpperCase();
  int n = activeNumLeds[s]; // Số bóng hiện tại của dải này

  // ── NUMLEDS: thay đổi số bóng LED cho dải ─────────────────
  if (action == "NUMLEDS") {
    int count = params.toInt();
    if (count >= 1 && count <= MAX_LEDS_PER_STRIP) {
      // Nếu giảm số lượng → tắt các bóng thừa
      if (count < activeNumLeds[s]) {
        for (int i = count; i < activeNumLeds[s]; i++) {
          leds[s][i] = CRGB::Black;
        }
      }
      activeNumLeds[s] = count;
      FastLED.show();
      Serial.print("OK LED ");
      Serial.print(s);
      Serial.print(" NUMLEDS ");
      Serial.println(count);
    } else {
      Serial.print("ERR NUMLEDS 1-");
      Serial.println(MAX_LEDS_PER_STRIP);
    }
    return;
  }

  // ── COLOR: đặt màu toàn bộ dải ────────────────────────────
  if (action == "COLOR") {
    int s1 = params.indexOf(' ');
    int s2 = params.indexOf(' ', s1 + 1);
    if (s1 > 0 && s2 > 0) {
      int r = params.substring(0, s1).toInt();
      int g = params.substring(s1 + 1, s2).toInt();
      int b = params.substring(s2 + 1).toInt();
      fill_solid(leds[s], n, CRGB(r, g, b));
      stripMode[s] = 0;
      FastLED.show();
      Serial.print("OK LED ");
      Serial.print(s);
      Serial.println(" COLOR");
    } else {
      Serial.println("ERR COLOR <r> <g> <b>");
    }
    return;
  }

  // ── BRIGHTNESS: độ sáng global ────────────────────────────
  if (action == "BRIGHTNESS") {
    int br = params.toInt();
    if (br >= 0 && br <= 255) {
      globalBrightness = br;
      FastLED.setBrightness(br);
      FastLED.show();
      Serial.print("OK BRIGHTNESS ");
      Serial.println(br);
    } else {
      Serial.println("ERR BRIGHTNESS 0-255");
    }
    return;
  }

  // ── CLEAR: tắt dải ────────────────────────────────────────
  if (action == "CLEAR") {
    fill_solid(leds[s], n, CRGB::Black);
    stripMode[s] = 0;
    FastLED.show();
    Serial.print("OK LED ");
    Serial.print(s);
    Serial.println(" CLEAR");
    return;
  }

  // ── MODE: chế độ hiệu ứng ─────────────────────────────────
  if (action == "MODE") {
    int m = params.toInt();
    if (m >= 0 && m <= 2) {
      stripMode[s] = m;
      fxState[s] = 0;
      Serial.print("OK LED ");
      Serial.print(s);
      Serial.print(" MODE ");
      Serial.println(m);
    } else {
      Serial.println("ERR MODE 0-2 (0=static,1=rainbow,2=chase)");
    }
    return;
  }

  // ── SPEED: tốc độ hiệu ứng ────────────────────────────────
  if (action == "SPEED") {
    int sp = params.toInt();
    if (sp >= 10 && sp <= 500) {
      stripSpeed[s] = sp;
      Serial.print("OK LED ");
      Serial.print(s);
      Serial.print(" SPEED ");
      Serial.println(sp);
    } else {
      Serial.println("ERR SPEED 10-500");
    }
    return;
  }

  // ── PIXEL: đặt 1 bóng cụ thể ──────────────────────────────
  if (action == "PIXEL") {
    int s1 = params.indexOf(' ');
    int s2 = params.indexOf(' ', s1 + 1);
    int s3 = params.indexOf(' ', s2 + 1);
    if (s1 > 0 && s2 > 0 && s3 > 0) {
      int idx = params.substring(0, s1).toInt();
      int r   = params.substring(s1 + 1, s2).toInt();
      int g   = params.substring(s2 + 1, s3).toInt();
      int b   = params.substring(s3 + 1).toInt();
      if (idx >= 0 && idx < n) {
        leds[s][idx] = CRGB(r, g, b);
        stripMode[s] = 0;
        FastLED.show();
        Serial.print("OK LED ");
        Serial.print(s);
        Serial.print(" PIXEL ");
        Serial.println(idx);
      } else {
        Serial.print("ERR INDEX 0-");
        Serial.println(n - 1);
      }
    } else {
      Serial.println("ERR PIXEL <idx> <r> <g> <b>");
    }
    return;
  }

  Serial.println("ERR UNKNOWN LED ACTION");
}

// =============================================================
//  PARSE SERIAL COMMAND
// =============================================================
void processSerialCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  // ── STATUS ─────────────────────────────────────────────────
  if (cmd.equalsIgnoreCase("STATUS")) {
    Serial.print("STATUS STRIPS=");
    Serial.print(NUM_STRIPS);
    Serial.print(" MAX_LEDS=");
    Serial.print(MAX_LEDS_PER_STRIP);
    Serial.print(" MICS=");
    Serial.print(NUM_MICS);
    Serial.print(" BRIGHT=");
    Serial.print(globalBrightness);
    Serial.print(" MIC_EN=");
    Serial.print(micEnabled ? "ON" : "OFF");
    Serial.print(" MIC_RATE=");
    Serial.print(micSampleRate);
    // In số bóng mỗi dải
    Serial.print(" LEDS=[");
    for (int i = 0; i < NUM_STRIPS; i++) {
      Serial.print(activeNumLeds[i]);
      if (i < NUM_STRIPS - 1) Serial.print(",");
    }
    Serial.print("] MODES=[");
    for (int i = 0; i < NUM_STRIPS; i++) {
      Serial.print(stripMode[i]);
      if (i < NUM_STRIPS - 1) Serial.print(",");
    }
    Serial.println("]");
    return;
  }

  // ── MIC commands ───────────────────────────────────────────
  if (cmd.equalsIgnoreCase("MIC PAUSE")) {
    micEnabled = false;
    Serial.println("OK MIC PAUSED");
    return;
  }
  if (cmd.equalsIgnoreCase("MIC RESUME")) {
    micEnabled = true;
    micSampling = false; // Reset sampling
    Serial.println("OK MIC RESUMED");
    return;
  }
  if (cmd.startsWith("MIC RATE ") || cmd.startsWith("mic rate ")) {
    int rate = cmd.substring(9).toInt();
    if (rate >= 20 && rate <= 500) {
      micSampleRate = rate;
      micSampling = false;
      Serial.print("OK MIC RATE ");
      Serial.println(rate);
    } else {
      Serial.println("ERR MIC RATE 20-500");
    }
    return;
  }

  // ── LED commands ───────────────────────────────────────────
  if (cmd.startsWith("LED ") || cmd.startsWith("led ")) {
    String rest = cmd.substring(4);
    rest.trim();

    int spaceIdx = rest.indexOf(' ');
    if (spaceIdx <= 0) {
      Serial.println("ERR LED <0-5|ALL> <ACTION> [params]");
      return;
    }

    String stripStr = rest.substring(0, spaceIdx);
    String remainder = rest.substring(spaceIdx + 1);
    remainder.trim();

    int actionSpace = remainder.indexOf(' ');
    String action, params;
    if (actionSpace > 0) {
      action = remainder.substring(0, actionSpace);
      params = remainder.substring(actionSpace + 1);
    } else {
      action = remainder;
      params = "";
    }

    stripStr.toUpperCase();
    if (stripStr == "ALL") {
      for (int i = 0; i < NUM_STRIPS; i++) {
        applyLedCommand(i, action, params);
      }
    } else {
      int stripNum = stripStr.toInt();
      if (stripNum < 0 || stripNum >= NUM_STRIPS) {
        Serial.print("ERR STRIP 0-");
        Serial.println(NUM_STRIPS - 1);
        return;
      }
      applyLedCommand(stripNum, action, params);
    }
    return;
  }

  Serial.println("ERR UNKNOWN. Use: LED/MIC/STATUS");
}

// =============================================================
//  MIC SAMPLING (Non-blocking)
// =============================================================
void micStartSampling() {
  for (int i = 0; i < NUM_MICS; i++) {
    signalMax[i] = 0;
    signalMin[i] = 1024;
  }
  micSampleStart = millis();
  micSampling = true;
}

void micCollectSample() {
  for (int i = 0; i < NUM_MICS; i++) {
    analogRead(micPins[i]);         // Đọc bỏ (xả tụ ADC MUX)
    delayMicroseconds(100);         // Đợi tụ ổn định
    unsigned int sample = analogRead(micPins[i]);
    if (sample < 1024) {
      if (sample > signalMax[i]) signalMax[i] = sample;
      if (sample < signalMin[i]) signalMin[i] = sample;
    }
  }
}

void micSendData() {
  Serial.print("MIC:");
  for (int i = 0; i < NUM_MICS; i++) {
    unsigned int pp = signalMax[i] - signalMin[i];
    Serial.print(pp);
    if (i < NUM_MICS - 1) Serial.print(",");
  }
  Serial.println();
}

// =============================================================
//  LED EFFECTS
// =============================================================
void updateEffects() {
  unsigned long now = millis();
  bool needShow = false;

  for (int s = 0; s < NUM_STRIPS; s++) {
    if (stripMode[s] == 0) continue; // Tĩnh → bỏ qua
    if (now - lastFxUpdate[s] < (unsigned long)stripSpeed[s]) continue;

    lastFxUpdate[s] = now;
    int n = activeNumLeds[s];

    if (stripMode[s] == 1) {
      // ── Cầu vồng ──
      fill_rainbow(leds[s], n, fxState[s], max(1, 255 / n));
      fxState[s]++;
      needShow = true;

    } else if (stripMode[s] == 2) {
      // ── Chase (1 bóng chạy) ──
      for (int i = 0; i < n; i++) {
        leds[s][i] = (i == fxState[s] % n) ? CRGB(255, 180, 0) : CRGB::Black;
      }
      fxState[s]++;
      if (fxState[s] >= n) fxState[s] = 0;
      needShow = true;
    }
  }

  if (needShow) FastLED.show();
}

// =============================================================
//  MAIN LOOP
// =============================================================
void loop() {
  // ── 1. Xử lý lệnh Serial ─────────────────────────────────
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    processSerialCommand(cmd);
  }

  // ── 2. Mic sampling ───────────────────────────────────────
  if (micEnabled) {
    if (!micSampling) {
      micStartSampling();
    }
    micCollectSample();
    if (millis() - micSampleStart >= (unsigned long)micSampleRate) {
      micSendData();
      micSampling = false;
    }
  }

  // ── 3. LED effects ────────────────────────────────────────
  updateEffects();
}
