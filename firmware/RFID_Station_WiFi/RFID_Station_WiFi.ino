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
static bool initRc522() {
  rfid.PCD_Init();
  delay(50);
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  rc522LastVersion = v;
  Serial.print(F("RC522 version = 0x"));
  Serial.println(v, HEX);
  return (v != 0x00 && v != 0xFF);
}

// บรรทัดล่างบอกค่าที่อ่านได้ ช่วยไล่สายได้เลยโดยไม่ต้องเปิด Serial Monitor
//   0x00 = อ่านได้ศูนย์ทุกบิต  -> ไฟเลี้ยง / SCK / MISO / SDA
//   0xFF = อ่านได้หนึ่งทุกบิต -> สาย MISO ลอย
// ตัวนับรอบบอกว่าเครื่องยังทำงานอยู่ ไม่ได้ค้าง
static void showRc522Error() {
  char line2[17];
  snprintf(line2, sizeof(line2), "v=0x%02X try %lu", rc522LastVersion, rc522Retries);
  lcdShow("RC522 ERROR", line2);
}

// ---------- ประกาศตัวเองให้เซิร์ฟเวอร์รู้จัก ----------
// ยิงตอนบูตและทุก ๆ ANNOUNCE_INTERVAL_MS เพื่อให้บอร์ดใหม่โผล่บนหน้าเว็บ
// ได้ตั้งแต่ก่อนมีใครเอาบัตรมาแตะ และให้หน้าเว็บรู้ว่าเครื่องไหนยังออนไลน์
static void announce() {
  if (!wifiReady()) return;

  String url = String("http://") + API_HOST + ":" + API_PORT + "/api/readers/announce";
  String body = String("{\"device_id\":\"") + DEVICE_ID +
                "\",\"mac\":\"" + WiFi.macAddress() +
                "\",\"firmware\":\"" + FIRMWARE_VERSION + "\"}";

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(wifiClient, url)) return;
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);
  Serial.print(F("announce -> "));
  Serial.println(code);
  if (code == 200) {
    String payload = http.getString();
    Serial.print(F("  ")); Serial.println(payload);
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
    // ต้องวาดซ้ำตรงนี้ เพราะข้อความ RC522 ERROR ที่ขึ้นไปก่อนหน้า
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

  // RC522 ยังไม่พร้อม ลองใหม่เป็นระยะ ไม่ปล่อยให้เครื่องตายค้าง
  if (!rc522Ok) {
    if (millis() - lastRc522Retry >= RC522_RETRY_MS) {
      lastRc522Retry = millis();
      rc522Retries++;
      rc522Ok = initRc522();
      if (rc522Ok) {
        Serial.println(F("RC522 recovered"));
        lcdShow("Ready", "Tap your card");
        beep(1, 80, 0);
        blink(3, 150, 150);
      } else {
        showRc522Error();   // อัปเดตตัวนับให้เห็นว่าเครื่องยังไม่ค้าง
      }
    }
    delay(50);
    return;
  }

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
