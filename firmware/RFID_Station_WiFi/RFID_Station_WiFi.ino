/*
 * RFID Station (WiFi) - อ่านบัตรแล้วถามเซิร์ฟเวอร์ว่าผ่านหรือไม่ผ่าน
 *
 * แก้ค่า WiFi / IP เซิร์ฟเวอร์ / รหัสเครื่อง ได้ที่ไฟล์ config.h
 *
 * ===== การต่อสาย =====
 * RC522 (SPI)              LCD 1602A (บอร์ด I2C ด้านหลัง)
 *   SDA/SS -> D8             GND -> GND
 *   SCK    -> D5             VCC -> VIN (5V)
 *   MOSI   -> D7             SDA -> D2
 *   MISO   -> D6             SCL -> D1
 *   RST    -> ไม่ต่อ
 *   VCC    -> 3V3          Buzzer (low-level trigger)
 *   GND    -> GND            VCC -> VIN (5V)
 *   IRQ    -> ไม่ต่อ         GND -> GND
 *                            I/O -> D0
 *
 * ไลบรารีที่ต้องมี: MFRC522, LiquidCrystal I2C (Frank de Brabander)
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <SPI.h>
#include <Wire.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>

#include "config.h"
#include "scan_result.h"

MFRC522 rfid(SS_PIN, MFRC522::UNUSED_PIN);   // ไม่ใช้ขา RST ใช้ soft reset แทน
LiquidCrystal_I2C *lcd = nullptr;            // สร้างหลังรู้แอดเดรสจากการกวาด I2C
WiFiClient wifiClient;

static bool rc522Ok = false;                 // RC522 ตอบสนองหรือยัง
static unsigned long lastWifiKick = 0;       // ครั้งสุดท้ายที่สั่งต่อ WiFi ใหม่
static unsigned long lastRc522Retry = 0;     // ครั้งสุดท้ายที่ลอง init RC522 ใหม่
static unsigned long lastAnnounce = 0;       // ครั้งสุดท้ายที่ประกาศตัวเอง
static unsigned long rc522Retries = 0;       // ลอง init RC522 ไปกี่รอบแล้ว
static byte rc522LastVersion = 0;            // ค่า VersionReg ล่าสุด ใช้ชี้ว่าสายเส้นไหนน่าจะหลุด
static byte rc522LastPrescaler = 0;          // TPrescalerReg ที่อ่านได้ล่าสุด ควรเป็น 0xA9
static byte rc522LastReload = 0;             // TReloadRegL ที่อ่านได้ล่าสุด ควรเป็น 0xE8
static const char *rc522Fault = "";          // สาเหตุที่ตรวจไม่ผ่านครั้งล่าสุด ว่าง = ปกติ
static int rc522OkReported = -1;             // สถานะ RC522 ที่เซิร์ฟเวอร์รับไปแล้ว -1 = ยังส่งไม่สำเร็จ
static unsigned long lastAnnounceTry = 0;    // ครั้งสุดท้ายที่พยายามประกาศตัว ไม่ว่าจะสำเร็จหรือไม่
static unsigned long lastRc522Health = 0;    // ครั้งสุดท้ายที่ตรวจว่า RC522 ยังทำงาน
static unsigned long lastRc522Reinit = 0;    // ครั้งสุดท้ายที่ init RC522 ใหม่แบบป้องกันไว้ก่อน
static unsigned long rc522Recoveries = 0;    // กู้ RC522 ที่ค้างกลับมาได้กี่ครั้งแล้ว

// ---------- บัซเซอร์ ----------
// จ่ายไฟ 5V แต่ ESP เป็นลอจิก 3.3V การสั่ง HIGH จึงปิดเสียงไม่ได้
// ต้องปล่อยขาให้ลอย (INPUT) แทน ซึ่งทดสอบแล้วว่าโมดูลจะเงียบ
static void buzzerOn()  { pinMode(BUZZER, OUTPUT); digitalWrite(BUZZER, LOW); }
static void buzzerOff() { pinMode(BUZZER, INPUT); }

static void beep(byte times, int onMs, int offMs) {
  for (byte i = 0; i < times; i++) {
    buzzerOn();  delay(onMs);
    buzzerOff(); if (offMs) delay(offMs);
  }
}

// ไฟบนบอร์ด (D4) เป็น active-low: LOW = ติด
// ถ้าต่อ LED 5mm เพิ่มแบบ 3V3 -[220R]- ขายาว / ขาสั้น -> D4 จะติดพร้อมกันเป๊ะ
static void ledOn()  { digitalWrite(LED_BUILTIN, LOW); }
static void ledOff() { digitalWrite(LED_BUILTIN, HIGH); }

static void blink(byte times, int onMs, int offMs) {
  for (byte i = 0; i < times; i++) {
    digitalWrite(LED_BUILTIN, LOW);  delay(onMs);
    digitalWrite(LED_BUILTIN, HIGH); if (offMs) delay(offMs);
  }
}

// ---------- จอ ----------
static void lcdLine(byte row, const String &text) {
  if (!lcd) return;
  String s = text;
  while (s.length() < 16) s += ' ';      // เติมช่องว่างให้เต็ม ลบข้อความเดิมทิ้ง
  lcd->setCursor(0, row);
  lcd->print(s.substring(0, 16));
}

static void lcdShow(const String &top, const String &bottom) {
  lcdLine(0, top);
  lcdLine(1, bottom);
}

static byte findLcdAddress() {
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) return addr;
  }
  return 0;
}

// ---------- อ่านค่าจาก JSON แบบง่าย ----------
// คำตอบจากเซิร์ฟเวอร์สั้นและรูปแบบแน่นอน จึงแกะเองได้ ไม่ต้องลงไลบรารี JSON เพิ่ม
// แต่ต้องรองรับ escape เพราะชื่อคนอาจมีเครื่องหมายคำพูด เช่น  Jay "JJ" Chen
static String jsonString(const String &src, const char *key) {
  String needle = String("\"") + key + "\":\"";
  int i = src.indexOf(needle);
  if (i < 0) return "";
  i += needle.length();

  String out;
  while (i < (int)src.length()) {
    char c = src[i];
    if (c == '\\') {
      // ข้ามแบ็กสแลชแล้วเอาตัวถัดไปตรง ๆ กันการจบสตริงกลางคัน
      i++;
      if (i >= (int)src.length()) break;
      char e = src[i];
      if      (e == 'n') out += '\n';
      else if (e == 't') out += '\t';
      else if (e == 'u') { out += '?'; i += 4; }   // \uXXXX แสดงบนจอ 1602 ไม่ได้อยู่แล้ว
      else               out += e;                // \" \\ \/ และอื่น ๆ
    } else if (c == '"') {
      break;                                      // เจอคำพูดปิดที่ไม่ได้ถูก escape
    } else {
      out += c;
    }
    i++;
  }
  return out;
}

static bool jsonBool(const String &src, const char *key) {
  String needle = String("\"") + key + "\":";
  int i = src.indexOf(needle);
  if (i < 0) return false;
  return src.substring(i + needle.length(), i + needle.length() + 4) == "true";
}

// ---------- WiFi ----------
// สั่งต่อใหม่แบบไม่รอ เพื่อไม่ให้คนที่มาแตะบัตรต้องยืนรอหน้าเครื่องเป็นสิบวินาที
static void kickWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);        // ไม่ต้องเขียน flash ทุกครั้งที่ต่อใหม่
  WiFi.setAutoReconnect(true);   // ให้ stack พยายามต่อเองเป็นพื้นหลัง
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiKick = millis();
}

static bool wifiReady() {
  if (WiFi.status() == WL_CONNECTED) return true;
  // ไม่กระตุ้นถี่เกินไป ปล่อยให้ auto-reconnect ทำงานเป็นหลัก
  if (millis() - lastWifiKick >= WIFI_RETRY_MS) kickWiFi();
  return false;
}

// รอตอนบูตได้ เพราะยังไม่มีใครยืนรอ แต่จำกัดเวลาไว้
static void waitForWiFi(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
}

// ---------- RC522 ----------
// ค่าที่ PCD_Init() ของไลบรารีเขียนลง register ตั้งเวลา และไม่มีฟังก์ชันอื่นในไลบรารีเขียนทับอีก
// (ตรวจแล้วใน MFRC522 1.4.12) ใช้ยืนยันว่าทางอ่านของ SPI ได้ค่าที่ถูกต้อง
static const byte RC522_TPRESCALER = 0xA9;
static const byte RC522_TRELOAD_L  = 0xE8;
// ค่าทดสอบการเขียน เป็นค่ากลับบิตของ 0xE8 พอเขียนสลับกับ 0xE8 จึงทดสอบครบทั้ง 8 บิตทั้งสองสถานะ
static const byte RC522_TRELOAD_TEST = 0x17;

// อ่าน register ที่บอกว่า RC522 พร้อมอ่านบัตรจริง ไม่ใช่แค่ยังตอบ register ได้
// เก็บค่าที่อ่านได้และสาเหตุไว้โชว์บนจอตอนเสีย
//
// เช็ค 4 อย่าง ตามลำดับที่ใช้บอกสาเหตุ
//   VersionReg   : 0x00/0xFF แปลว่าไม่มีชิปตอบเลย                                   -> NOCHIP
//   TPrescalerReg/TReloadRegL : ต้องได้ค่าที่ PCD_Init เขียนไว้ ถ้าชิปถูกรีเซ็ตเองจะกลับเป็น 0
//                  และถ้าสาย SDA (D8) หลุด ค่าที่อ่านได้จะเป็นขยะที่ไม่ใช่ 0x00/0xFF
//                  ซึ่งถ้าดูแค่ VersionReg จะผ่านและรายงานว่าปกติทั้งที่อ่านบัตรไม่ได้ (บั๊กของ 1.3.0) -> READ
//   เขียนแล้วอ่านกลับ : ค่าที่อ่านได้อย่างเดียวพิสูจน์ไม่ได้ว่าเขียนติด เพราะอาจเป็นค่าค้างจาก init ครั้งก่อน
//                  เช่นสาย SDA ค้าง LOW จนชิปมองทุกไบต์เป็นคำสั่งอ่าน อ่านถูกหมดแต่เขียนไม่ติดเลย (พบจากรีวิว 1.3.1)
//                  ใช้ TReloadRegL เพราะใช้แค่ตั้ง timeout ตอนคุยกับบัตร เขียนสลับตอนว่างได้ แล้วคืน 0xE8 ทุกครั้ง -> WRITE
//   TxControlReg : บิต 0-1 คือสายอากาศ ถ้าดับไปจะตอบ register ได้ปกติแต่อ่านบัตรไม่ได้เลย          -> ANT
static bool rc522Registers(byte &version) {
  version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  rc522LastPrescaler = rfid.PCD_ReadRegister(MFRC522::TPrescalerReg);
  rc522LastReload = rfid.PCD_ReadRegister(MFRC522::TReloadRegL);
  byte tx = rfid.PCD_ReadRegister(MFRC522::TxControlReg);

  rfid.PCD_WriteRegister(MFRC522::TReloadRegL, RC522_TRELOAD_TEST);
  bool wroteTest = rfid.PCD_ReadRegister(MFRC522::TReloadRegL) == RC522_TRELOAD_TEST;
  rfid.PCD_WriteRegister(MFRC522::TReloadRegL, RC522_TRELOAD_L);
  bool wroteBack = rfid.PCD_ReadRegister(MFRC522::TReloadRegL) == RC522_TRELOAD_L;

  if (version == 0x00 || version == 0xFF)  rc522Fault = "NOCHIP";
  else if (rc522LastPrescaler != RC522_TPRESCALER ||
           rc522LastReload != RC522_TRELOAD_L)  rc522Fault = "READ";
  else if (!wroteTest || !wroteBack)       rc522Fault = "WRITE";
  else if ((tx & 0x03) != 0x03)            rc522Fault = "ANT";
  else                                     rc522Fault = "";
  return rc522Fault[0] == '\0';
}

// ใช้เกณฑ์เดียวกับการตรวจสุขภาพ ไม่งั้น init จะผ่านง่ายกว่าการตรวจ
// แล้ววนกู้ไม่รู้จบทุก 10 วินาทีพร้อมรายงานว่าปกติ
static bool initRc522() {
  rfid.PCD_Init();
  delay(50);
  byte v;
  bool ok = rc522Registers(v);
  rc522LastVersion = v;
  lastRc522Health = millis();
  lastRc522Reinit = millis();
  Serial.print(F("RC522 version = 0x"));
  Serial.print(v, HEX);
  Serial.print(F(", timer = 0x"));
  Serial.print(rc522LastPrescaler, HEX);
  Serial.print(F("/0x"));
  Serial.print(rc522LastReload, HEX);
  if (ok) {
    Serial.println(F(" -> ok"));
  } else {
    Serial.print(F(" -> FAIL "));
    Serial.println(rc522Fault);
  }
  return ok;
}

// ตรวจว่า RC522 ยังพร้อมอ่านบัตรจริงระหว่างที่รอบัตร
//
// ที่ต้องมีเพราะ RC522 (โดยเฉพาะตัวเลียนแบบ) ชอบหยุดตรวจจับบัตรเองเวลาเปิดทิ้งไว้นาน ๆ
// โดยไม่มีสัญญาณบอกอะไรเลย PICC_IsNewCardPresent() แค่ตอบ false ไปเรื่อย ๆ
// จอจึงค้างอยู่ที่ Ready ทั้งคืนแบบที่เจอกับ WC04/WC05
// นอกจากเกณฑ์ของ rc522Registers แล้ว VersionReg ต้องไม่เปลี่ยนไปจากตอน init ด้วย
static bool rc522Healthy() {
  byte v;
  return rc522Registers(v) && v == rc522LastVersion;
}

// ดูแล RC522 ระหว่างที่รอบัตร คืนค่า false ถ้า RC522 เสียจนกู้ไม่ได้ในรอบนี้
static bool maintainRc522() {
  unsigned long now = millis();

  if (now - lastRc522Health >= RC522_HEALTH_MS) {
    lastRc522Health = now;
    if (!rc522Healthy()) {
      byte before = rc522LastVersion;
      Serial.println(F("!! RC522 stopped responding - reinitialising"));
      rc522Ok = initRc522();
      if (!rc522Ok) {
        rc522Retries = 0;          // นับรอบลองใหม่ของการเสียครั้งนี้ ตัวเลขบนจอจะได้มีความหมาย
        showRc522Error();          // loop รอบหน้าจะไปเข้าทางลองใหม่ทุก 3 วินาทีเอง
        announce();                // แจ้งหน้าเว็บทันที ไม่ต้องรอรอบ 5 นาที
        return false;
      }
      rc522Recoveries++;
      lastRc522Reinit = now;
      Serial.print(F("RC522 recovered, version 0x"));
      Serial.print(before, HEX);
      Serial.print(F(" -> 0x"));
      Serial.print(rc522LastVersion, HEX);
      Serial.print(F(", recoveries = "));
      Serial.println(rc522Recoveries);
      lcdShow("Ready", "Tap your card");
      announce();
    }
  }

  // init ใหม่เป็นระยะแม้ผลตรวจจะปกติ เพราะเจอกรณีที่ RC522 ยังตอบ register ถูกหมด
  // แต่ตรวจจับบัตรไม่ได้แล้ว ใช้เวลาแค่ราว 50ms ทำตอนว่างจึงไม่กระทบใคร
  if (RC522_REINIT_MS > 0 && now - lastRc522Reinit >= RC522_REINIT_MS) {
    lastRc522Reinit = now;
    rfid.PCD_Init();
    rfid.PCD_AntennaOn();
  }
  return true;
}

// บอกสาเหตุและค่าที่อ่านได้ ช่วยไล่สายได้เลยโดยไม่ต้องเปิด Serial Monitor เช่น
//   RC522 ERR:READ
//   v=EE t=D6DA 12
// สาเหตุบนบรรทัดบน
//   NOCHIP -> v=00 อ่านได้ศูนย์ทุกบิต (ไฟเลี้ยง / SCK / MISO / SDA) หรือ v=FF (สาย MISO ลอย)
//   READ   -> อ่านได้แต่ค่าไม่ถูก คุยกับชิปได้ไม่ครบ มักเป็นสาย SDA (D8) หรือ MOSI (D7)
//   WRITE  -> อ่านได้ถูกแต่เขียนไม่ติด มักเป็นสาย SDA (D8) ค้าง หรือ MOSI (D7) หลุด
//   ANT    -> ชิปตอบปกติแต่สายอากาศไม่ยอมเปิด
// ตัวเลขท้ายสุดคือจำนวนรอบที่ลองใหม่ของการเสียครั้งนี้ บอกว่าเครื่องยังทำงานอยู่ ไม่ได้ค้าง
// วนกลับที่ 10000 ให้พอดีจอ 16 ตัวอักษรและยังขยับทุกรอบ ถ้าปล่อยให้เกินจะถูกตัดหลักท้ายจนดูเหมือนค้าง
static void showRc522Error() {
  char line1[17];
  char line2[17];
  snprintf(line1, sizeof(line1), "RC522 ERR:%s", rc522Fault);
  snprintf(line2, sizeof(line2), "v=%02X t=%02X%02X %lu",
           rc522LastVersion, rc522LastPrescaler, rc522LastReload, rc522Retries % 10000UL);
  lcdShow(line1, line2);
}

// ---------- ประกาศตัวเองให้เซิร์ฟเวอร์รู้จัก ----------
// ยิงตอนบูตและทุก ๆ ANNOUNCE_INTERVAL_MS เพื่อให้บอร์ดใหม่โผล่บนหน้าเว็บ
// ได้ตั้งแต่ก่อนมีใครเอาบัตรมาแตะ และให้หน้าเว็บรู้ว่าเครื่องไหนยังออนไลน์
static void announce() {
  lastAnnounceTry = millis();
  if (!wifiReady()) return;

  String url = String("http://") + API_HOST + ":" + API_PORT + "/api/readers/announce";
  // สุขภาพของ RC522 ส่งไปด้วยทุกครั้ง หน้าเว็บจะได้โชว์ว่าเครื่องไหนหลุดบ่อย
  // uptime ให้ backend รู้ว่าบอร์ดรีสตาร์ทไปหรือยัง จะได้สะสมยอดการกู้ได้ถูก
  String body = String("{\"device_id\":\"") + DEVICE_ID +
                "\",\"mac\":\"" + WiFi.macAddress() +
                "\",\"firmware\":\"" + FIRMWARE_VERSION +
                "\",\"rc522_ok\":" + (rc522Ok ? "true" : "false") +
                ",\"rc522_recoveries\":" + String(rc522Recoveries) +
                ",\"uptime_s\":" + String(millis() / 1000UL) + "}";

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(wifiClient, url)) return;
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);
  Serial.print(F("announce -> "));
  Serial.print(code);
  // ข้อมูลไว้ไล่ปัญหาเวลาเปิดทิ้งไว้นาน ๆ: หน่วยความจำเหลือกี่ไบต์ กู้ RC522 ไปกี่ครั้ง
  Serial.print(F("  uptime "));
  Serial.print(millis() / 60000UL);
  Serial.print(F(" min, heap "));
  Serial.print(ESP.getFreeHeap());
  Serial.print(F(", rc522 recoveries "));
  Serial.println(rc522Recoveries);
  if (code == 200) {
    String payload = http.getString();
    Serial.print(F("  ")); Serial.println(payload);
    rc522OkReported = rc522Ok ? 1 : 0;   // เซิร์ฟเวอร์รับสถานะนี้ไปแล้วจริง
  }
  http.end();
  lastAnnounce = millis();
}

// ---------- ส่งข้อมูลการแตะบัตร ----------
static ScanResult sendScan(const String &uid, const String &cardType) {
  ScanResult r{false, false, false, "", "", ""};

  // ไม่เรียก connect แบบบล็อกที่นี่ ถ้า WiFi ยังไม่พร้อมก็ตอบกลับทันที
  // แล้วให้ auto-reconnect ตามไปเอง คนแตะบัตรจะได้ไม่ต้องรอ
  if (!wifiReady()) return r;
  r.wifiUp = true;

  String url = String("http://") + API_HOST + ":" + API_PORT + "/api/scan";
  String body = String("{\"uid\":\"") + uid +
                "\",\"card_type\":\"" + cardType +
                "\",\"device_id\":\"" + DEVICE_ID +
                "\",\"mac\":\"" + WiFi.macAddress() + "\"}";

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(wifiClient, url)) {
    Serial.println(F("!! http.begin failed"));
    return r;
  }
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);
  Serial.print(F("POST ")); Serial.print(url);
  Serial.print(F(" -> ")); Serial.println(code);

  if (code == 200) {
    String payload = http.getString();
    Serial.print(F("resp: ")); Serial.println(payload);
    r.reachedServer = true;
    r.allowed = jsonBool(payload, "allowed");
    r.name    = jsonString(payload, "name");
    r.barcode = jsonString(payload, "barcode");
    r.message = jsonString(payload, "message");
  }
  http.end();
  return r;
}

// ---------- setup ----------
void setup() {
  buzzerOff();                      // ปิดเสียงเป็นอย่างแรก กันร้องค้างตอนบูต
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println(F("=== RFID Station (WiFi) ==="));
  Serial.print(F("device id: ")); Serial.println(DEVICE_ID);
  Serial.print(F("firmware : ")); Serial.println(FIRMWARE_VERSION);
  Serial.print(F("mac      : ")); Serial.println(WiFi.macAddress());

  // จอ
  Wire.begin(SDA_PIN, SCL_PIN);
  byte addr = findLcdAddress();
  if (addr) {
    Serial.print(F("LCD at 0x")); Serial.println(addr, HEX);
    lcd = new LiquidCrystal_I2C(addr, 16, 2);
    lcd->init();
    lcd->backlight();
    lcdShow("RFID Station", DEVICE_ID);
  } else {
    Serial.println(F("!! LCD NOT FOUND"));
  }
  delay(700);

  // เครื่องอ่านบัตร - ถ้ายังไม่ตอบก็ไม่ออกจาก setup แต่จะไปลองใหม่ใน loop
  // (ตอนเปิดเครื่องจากปลั๊กสวิตช์ ไฟ 3.3V ขึ้นช้า ครั้งแรกอาจอ่านไม่ได้)
  SPI.begin();
  rc522Ok = initRc522();
  if (!rc522Ok) {
    Serial.println(F("!! RC522 not responding yet - will retry in loop"));
    showRc522Error();
    beep(3, 300, 150);
  }

  // WiFi - ต่อตอนบูตได้เพราะยังไม่มีใครยืนรอ
  lcdShow("Connecting WiFi", WIFI_SSID);
  kickWiFi();
  Serial.print(F("WiFi connecting"));
  waitForWiFi(WIFI_BOOT_TIMEOUT_MS);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("WiFi OK, IP = "));
    Serial.println(WiFi.localIP());
    lcdShow("WiFi connected", WiFi.localIP().toString());
  } else {
    Serial.println(F("!! WiFi not connected yet - will keep retrying"));
    lcdShow("WiFi retrying", WIFI_SSID);
  }
  delay(1200);

  // บอกเซิร์ฟเวอร์ว่าเครื่องนี้ออนไลน์แล้ว พร้อม MAC และ IP
  announce();

  if (rc522Ok) {
    lcdShow("Ready", "Tap your card");
    beep(1, 80, 0);
    blink(3, 150, 150);
  } else {
    // ต้องวาดซ้ำตรงนี้ เพราะข้อความ RC522 ERR ที่ขึ้นไปก่อนหน้า
    // ถูกหน้าจอ Connecting WiFi / WiFi connected เขียนทับไปแล้ว
    // ถ้าไม่วาดใหม่ จอจะค้างที่หน้า WiFi จนดูเหมือนปัญหาอยู่ที่ WiFi
    showRc522Error();
  }
}

// ---------- loop ----------
void loop() {
  // งาน WiFi กับการประกาศตัวต้องทำทุกรอบ แม้ RC522 จะเสียอยู่ก็ตาม
  // ไม่งั้นบอร์ดจะหายเงียบจากหน้าเว็บ แล้วดูไม่ออกว่าเครื่องดับ
  // หรือแค่เครื่องอ่านบัตรมีปัญหา
  wifiReady();
  if (millis() - lastAnnounce >= ANNOUNCE_INTERVAL_MS) announce();
  // สถานะ RC522 เปลี่ยนแต่ส่งไม่ถึงเซิร์ฟเวอร์ (WiFi หลุด / เซิร์ฟเวอร์รีสตาร์ทพอดี) ให้ส่งซ้ำจนกว่าจะรับ
  // ไม่งั้นหน้าเว็บจะโชว์สถานะเก่าไปอีกถึง 5 นาที ทั้งที่จอบอกว่าเสียแล้ว
  else if ((rc522Ok ? 1 : 0) != rc522OkReported &&
           millis() - lastAnnounceTry >= ANNOUNCE_RETRY_MS) announce();

  // RC522 ยังไม่พร้อม ลองใหม่เป็นระยะ ไม่ปล่อยให้เครื่องตายค้าง
  if (!rc522Ok) {
    if (millis() - lastRc522Retry >= RC522_RETRY_MS) {
      lastRc522Retry = millis();
      rc522Retries++;
      rc522Ok = initRc522();
      if (rc522Ok) {
        Serial.println(F("RC522 recovered"));
        rc522Recoveries++;       // นับด้วย เป็นการกู้กลับมาเหมือนกัน
        lcdShow("Ready", "Tap your card");
        beep(1, 80, 0);
        blink(3, 150, 150);
        announce();
      } else {
        showRc522Error();   // อัปเดตตัวนับให้เห็นว่าเครื่องยังไม่ค้าง
      }
    }
    delay(50);
    return;
  }

  // รีสตาร์ทตัวเองวันละครั้งตอนว่าง เป็นตาข่ายรองรับอีกชั้น
  // ล้างสถานะแปลก ๆ ที่สะสมจากการเปิดยาว ๆ ทั้งของ RC522, WiFi stack และหน่วยความจำ
  // ที่แตกเป็นชิ้นจากการต่อ String ทำตรงนี้เพราะแน่ใจว่าไม่มีบัตรกำลังถูกประมวลผลอยู่
  if (DAILY_RESTART_MS > 0 && millis() >= DAILY_RESTART_MS) {
    Serial.println(F("scheduled restart"));
    lcdShow("Restarting...", DEVICE_ID);
    delay(300);
    ESP.restart();
  }

  if (!maintainRc522()) return;

  if (!rfid.PICC_IsNewCardPresent()) { delay(10); return; }
  if (!rfid.PICC_ReadCardSerial())   { delay(10); return; }

  String uid;
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += '0';
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  String type = rfid.PICC_GetTypeName(rfid.PICC_GetType(rfid.uid.sak));
  Serial.print(F("card ")); Serial.print(uid); Serial.print(F(" / ")); Serial.println(type);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  lcdShow("Checking...", uid);

  ScanResult res = sendScan(uid, type);

  if (!res.wifiUp) {
    // แยกให้เห็นว่าเป็นปัญหา WiFi ไม่ใช่เซิร์ฟเวอร์ จะได้ไล่ปัญหาถูกจุด
    lcdShow("NO WIFI", uid);
    beep(4, 100, 80);
    blink(5, 120, 100);
  } else if (!res.reachedServer) {
    // ต่อเซิร์ฟเวอร์ไม่ได้ -> ปฏิเสธไว้ก่อน ปลอดภัยกว่าปล่อยผ่าน
    lcdShow("NO SERVER", uid);
    beep(5, 100, 80);
    blink(5, 120, 100);
  } else if (res.allowed) {
    // บรรทัดล่างโชว์บาร์โค้ด ถ้าบัตรใบนั้นยังไม่ได้ผูกบาร์โค้ดไว้จึงค่อยใช้ชื่อแทน
    // ไม่งั้นจอจะว่างเปล่าแล้วดูเหมือนเครื่องค้าง
    lcdShow(res.message, res.barcode.length() ? res.barcode : res.name);
    // เปิดไฟพร้อมเสียง แล้วค้างไว้ให้ครบ LED_OK_MS
    // เดิมกระพริบ 80ms สองครั้ง รวมแค่ 0.16 วินาที มองจากไฟจุดเล็กบนโมดูลแทบไม่ทัน
    ledOn();
    beep(1, 120, 0);                         // บี๊บสั้น 1 ครั้ง
    delay(LED_OK_MS > 120 ? LED_OK_MS - 120 : 0);
    ledOff();
  } else {
    // โชว์ UID บนบรรทัดแรกด้วย เพื่อเอาไปเทียบกับที่ขึ้นบนหน้าเว็บได้
    // "DENIED " + UID 4 ไบต์ = 15 ตัวอักษร พอดีกับจอ 16 ตัว
    String top = "DENIED " + uid;
    if (top.length() > 16) top = uid;        // UID 7 ไบต์ยาวเกิน โชว์ UID เดี่ยว ๆ ให้ครบ
    lcdShow(top, res.name.length() ? res.name : "Unknown card");
    beep(2, 200, 120);                       // บี๊บยาว 2 ครั้ง ต่างจากตอนผ่านชัดเจน
    blink(3, 250, 150);                      // กระพริบ 3 ครั้ง ต่างจากตอนผ่านที่ติดค้าง
  }

  // ค้างผลของบัตรใบล่าสุดไว้บนจอ จนกว่าจะมีบัตรใบใหม่มาแตะ
}
