// =============================================================
//  EU-SCPD MIC Firmware - Arduino Mega 2560
//  6x MAX4466 Microphones (A0-A5)
// =============================================================
//
//  Serial Output (115200 baud):
//    p0,p1,p2,p3,p4,p5        ← peak-to-peak mỗi 50ms
//
//  Serial Commands (PC → Arduino):
//    MIC PAUSE                 ← Tạm dừng gửi dữ liệu
//    MIC RESUME                ← Tiếp tục gửi
//    MIC RATE <20-500>         ← Thay đổi chu kỳ lấy mẫu (ms)
//    STATUS                    ← Xem trạng thái
// =============================================================

const int NUM_MICS = 6;
const int micPins[6] = {A0, A1, A2, A3, A4, A5};
const int SAMPLE_WINDOW_DEFAULT = 50; // 50ms = 20Hz

int  sampleWindow = SAMPLE_WINDOW_DEFAULT;
bool micEnabled   = true;

void setup() {
  Serial.begin(115200);
  Serial.println("READY EU-SCPD 6MIC");
}

void loop() {
  // ── Xử lý lệnh Serial ──────────────────────────────────
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.equalsIgnoreCase("MIC PAUSE")) {
      micEnabled = false;
      Serial.println("OK MIC PAUSED");
    }
    else if (cmd.equalsIgnoreCase("MIC RESUME")) {
      micEnabled = true;
      Serial.println("OK MIC RESUMED");
    }
    else if (cmd.startsWith("MIC RATE ") || cmd.startsWith("mic rate ")) {
      int rate = cmd.substring(9).toInt();
      if (rate >= 20 && rate <= 500) {
        sampleWindow = rate;
        Serial.print("OK MIC RATE ");
        Serial.println(rate);
      } else {
        Serial.println("ERR MIC RATE 20-500");
      }
    }
    else if (cmd.equalsIgnoreCase("STATUS")) {
      Serial.print("STATUS MICS=");
      Serial.print(NUM_MICS);
      Serial.print(" RATE=");
      Serial.print(sampleWindow);
      Serial.print(" EN=");
      Serial.println(micEnabled ? "ON" : "OFF");
    }
    else {
      Serial.println("ERR UNKNOWN. Use: MIC PAUSE/RESUME/RATE, STATUS");
    }
  }

  // ── Lấy mẫu mic (blocking trong sampleWindow ms) ───────
  if (!micEnabled) return;

  unsigned long startMillis = millis();
  unsigned int signalMax[6];
  unsigned int signalMin[6];

  for (int i = 0; i < NUM_MICS; i++) {
    signalMax[i] = 0;
    signalMin[i] = 1024;
  }

  while (millis() - startMillis < (unsigned long)sampleWindow) {
    for (int i = 0; i < NUM_MICS; i++) {
      analogRead(micPins[i]);       // Đọc bỏ (xả tụ ADC MUX)
      delayMicroseconds(100);       // Đợi tụ ổn định
      unsigned int sample = analogRead(micPins[i]);

      if (sample < 1024) {
        if (sample > signalMax[i]) signalMax[i] = sample;
        if (sample < signalMin[i]) signalMin[i] = sample;
      }
    }

    // Kiểm tra Serial giữa chừng để không bỏ lỡ lệnh
    if (Serial.available()) break;
  }

  // ── Gửi dữ liệu ───────────────────────────────────────
  for (int i = 0; i < NUM_MICS; i++) {
    unsigned int peakToPeak = signalMax[i] - signalMin[i];
    Serial.print(peakToPeak);
    if (i < NUM_MICS - 1) Serial.print(",");
  }
  Serial.println();
}
