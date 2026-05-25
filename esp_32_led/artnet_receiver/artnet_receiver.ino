/*
 * ESP32 Art-Net Receiver (Ethernet W5500) → WS2812 (6 Dây LED độc lập)
 * ======================================================================
 * 
 * Nhận Art-Net DMX qua ETHERNET W5500, xuất ra 6 dây WS2812 trên 6 GPIO khác nhau.
 * Mỗi dây LED được map với 1 Art-Net universe riêng.
 * 
 * Sơ đồ GPIO:
 *   Wire 0: GPIO 4  → Universe 0  (qua TXS0108E)
 *   Wire 1: GPIO 16 → Universe 1
 *   Wire 2: GPIO 33 → Universe 2
 *   Wire 3: GPIO 25 → Universe 3
 *   Wire 4: GPIO 26 → Universe 4
 *   Wire 5: GPIO 27 → Universe 5
 * 
 * Sơ đồ W5500:
 *   MISO: 19 | MOSI: 23 | SCK: 18 | CS: 5
 * 
 * App config (artnet_outputs): set IP = 192.168.2.50, universe_start = wire index
 */

#include <SPI.h>
#include <Ethernet.h>
#include <FastLED.h>
#include <ArtnetEther.h>

// ─── Ethernet Configuration ──────────────────────────────────────────────────
#define W5500_CS_PIN 5
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress staticIP(192, 168, 2, 50);

// ─── LED Configuration ─────────────────────────────────────────────────────
#define NUM_WIRES     6       // Số dây LED (tối đa 6)
#define LEDS_PER_WIRE 90      // Số LED mỗi dây
#define BRIGHTNESS    255     // Độ sáng (0-255)
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB

const uint8_t LED_PINS[NUM_WIRES] = { 4, 16, 33, 25, 26, 27 };

// Art-Net universe mapping: Wire N nhận universe N
#define UNIVERSE_START 0
#define EXPECTED_UNIVERSES NUM_WIRES
#define FRAME_FLUSH_MS 8
#define MIN_SHOW_INTERVAL_MS 20

// ─── Globals ────────────────────────────────────────────────────────────────
CRGB leds[NUM_WIRES][LEDS_PER_WIRE];
ArtnetEtherReceiver artnet;

// Stats
volatile uint32_t packetCount = 0;
uint32_t lastStatTime = 0;
uint32_t lastPacketTime = 0;
bool isReceiving = false;
uint8_t universeSeenMask = 0;
uint32_t frameStartTime = 0;
uint32_t lastShowTime = 0;
bool framePending = false;
uint32_t uniPacketCount[NUM_WIRES] = {0};

// ─── Setup ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println();
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║  ESP32 Art-Net → WS2812 (W5500 ETHERNET) ║");
  Serial.println("║  Static IP: 192.168.2.50                ║");
  Serial.println("╚══════════════════════════════════════════╝");
  
  // ── Init FastLED cho mỗi dây ──
  FastLED.addLeds<LED_TYPE, 4,  COLOR_ORDER>(leds[0], LEDS_PER_WIRE);
  FastLED.addLeds<LED_TYPE, 16, COLOR_ORDER>(leds[1], LEDS_PER_WIRE);
  FastLED.addLeds<LED_TYPE, 33, COLOR_ORDER>(leds[2], LEDS_PER_WIRE);
  FastLED.addLeds<LED_TYPE, 25, COLOR_ORDER>(leds[3], LEDS_PER_WIRE);
  FastLED.addLeds<LED_TYPE, 26, COLOR_ORDER>(leds[4], LEDS_PER_WIRE);
  FastLED.addLeds<LED_TYPE, 27, COLOR_ORDER>(leds[5], LEDS_PER_WIRE);
  FastLED.setBrightness(BRIGHTNESS);
  
  FastLED.clear();
  FastLED.show();
  
  // ── Khởi tạo W5500 Ethernet ──
  Ethernet.init(W5500_CS_PIN);
  
  Serial.println("Initializing Ethernet with Static IP...");
  Ethernet.begin(mac, staticIP);

  // Kiểm tra phần cứng
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("❌ ERROR: W5500 module was not found. Please check wiring (CS=5, SCK=18, MISO=19, MOSI=23).");
    while (true) { delay(1); } // Halt
  }
  
  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("⚠ Warning: Ethernet cable is not connected.");
  }
  
  Serial.println();
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.printf( "║  Ethernet Ready!                         ║\n");
  Serial.printf( "║  IP: %-36s║\n", Ethernet.localIP().toString().c_str());
  Serial.printf( "║  Art-Net Port: 6454                      ║\n");
  Serial.printf( "║  Universes: %d → %d                       ║\n", UNIVERSE_START, UNIVERSE_START + NUM_WIRES - 1);
  Serial.println("╚══════════════════════════════════════════╝");
  
  // ── Khởi tạo Art-Net và Callback ──
  artnet.begin();
  
  // Dùng mảng callback tĩnh do thư viện yêu cầu static callback hoặc lambda không capture
  // Có thể dùng một hàm callback chung, ta sẽ parse từ metadata.universe
  artnet.subscribeArtDmx([&](const uint8_t* data, uint16_t size, const ArtDmxMetadata &metadata, const ArtNetRemoteInfo &remote) {
    int wireIndex = metadata.universe - UNIVERSE_START;
    if (wireIndex < 0 || wireIndex >= NUM_WIRES) return;

    int numLeds = min((int)(size / 3), LEDS_PER_WIRE);
    for (int i = 0; i < numLeds; i++) {
      leds[wireIndex][i].r = data[i * 3];
      leds[wireIndex][i].g = data[i * 3 + 1];
      leds[wireIndex][i].b = data[i * 3 + 2];
    }
    for (int i = numLeds; i < LEDS_PER_WIRE; i++) {
      leds[wireIndex][i] = CRGB::Black;
    }

    if (!framePending) {
      frameStartTime = millis();
      universeSeenMask = 0;
      framePending = true;
    }
    universeSeenMask |= (1 << wireIndex);

    packetCount++;
    uniPacketCount[wireIndex]++;
    lastPacketTime = millis();
    isReceiving = true;
  });
  
  // Flash báo hiệu khởi động xong
  for (int w = 0; w < NUM_WIRES; w++) fill_solid(leds[w], LEDS_PER_WIRE, CRGB(0, 0, 30)); // Xanh dương nhẹ
  FastLED.show();
  delay(300);
  FastLED.clear();
  FastLED.show();
  
  lastStatTime = millis();
}

// ─── Main Loop ──────────────────────────────────────────────────────────────
void loop() {
  // Drain một burst UDP trước khi show LED. Việc này giúp W5500 không bị tràn
  // khi app gửi liên tiếp 6 universe cho cùng một frame.
  for (int i = 0; i < 12; i++) {
    if (artnet.parse() == art_net::OpCode::NoPacket) break;
  }
  
  uint8_t allUniversesMask = (1 << EXPECTED_UNIVERSES) - 1;
  bool gotFullFrame = (universeSeenMask & allUniversesMask) == allUniversesMask;
  bool frameTimedOut = framePending && (millis() - frameStartTime >= FRAME_FLUSH_MS);
  bool showIntervalReady = millis() - lastShowTime >= MIN_SHOW_INTERVAL_MS;
  if (framePending && showIntervalReady && (gotFullFrame || frameTimedOut)) {
    FastLED.show();
    lastShowTime = millis();
    universeSeenMask = 0;
    framePending = false;
  }
  
  // Tự động tắt đèn nếu quá 5 giây không nhận tín hiệu
  if (isReceiving && (millis() - lastPacketTime > 5000)) {
    Serial.println("⚠ Art-Net timeout - no data for 5s. LEDs off.");
    FastLED.clear();
    FastLED.show();
    universeSeenMask = 0;
    framePending = false;
    isReceiving = false;
  }
  
  // In stats mỗi 5 giây
  if (millis() - lastStatTime >= 5000) {
    float pps = packetCount / 5.0;
    Serial.printf("📊 Stats: %.1f pkts/sec | Cable: %s | IP: %s\n",
                  pps,
                  Ethernet.linkStatus() == LinkON ? "Connected" : "Disconnected",
                  Ethernet.localIP().toString().c_str());
                  
    Serial.printf("   Universes: [0]:%d [1]:%d [2]:%d [3]:%d [4]:%d [5]:%d\n",
                  uniPacketCount[0], uniPacketCount[1], uniPacketCount[2],
                  uniPacketCount[3], uniPacketCount[4], uniPacketCount[5]);
                  
    packetCount = 0;
    for(int i=0; i<NUM_WIRES; i++) uniPacketCount[i] = 0;
    lastStatTime = millis();
  }
}
