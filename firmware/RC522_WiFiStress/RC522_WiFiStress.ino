/*
 * RC522_WiFiStress - แยกว่า RC522 ตายเพราะ WiFi (ไฟตก / สัญญาณรบกวน) หรือตายเอง
 *
 * ขั้นตอน
 *   A) เปิด RC522 และวนถามหาบัตรเหมือนเครื่องจริง แต่ปิด WiFi สนิท
 *      รอจนได้รับตัว 'g' ทาง Serial (หรือครบ 120 วินาที) แล้วนับต่ออีก 40 วินาที
 *   B) เปิด WiFi แล้วต่อ AP ด้วยค่าเดียวกับเครื่องจริง ดูต่ออีก 60 วินาที
 *   C) ถ้า RC522 ตายระหว่างทาง ลอง PCD_Init ดูว่ากู้ด้วย soft reset ได้หรือไม่
 *
 * พิมพ์ผลตรวจทุกครั้งที่ผลเปลี่ยน และพิมพ์ซ้ำทุก 5 วินาที ไม่พิมพ์ชื่อหรือรหัส WiFi
 * ไม่ส่งอะไรเข้าเซิร์ฟเวอร์ ใช้เสร็จแล้วต้องอัปโหลด RFID_Station_WiFi กลับ
 */
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include "D:/Arduino/rfid-system/firmware/RFID_Station_WiFi/secrets.h"

MFRC522 rfid(D8, MFRC522::UNUSED_PIN);

static char lastLine[80] = "";
static unsigned long lastPrint = 0;
static unsigned long phaseStart = 0;
static char phase = 'A';
static bool everFailed = false;

static bool health(char *out, size_t n) {
  byte v  = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  byte p  = rfid.PCD_ReadRegister(MFRC522::TPrescalerReg);
  byte r  = rfid.PCD_ReadRegister(MFRC522::TReloadRegL);
  byte tx = rfid.PCD_ReadRegister(MFRC522::TxControlReg);
  rfid.PCD_WriteRegister(MFRC522::TReloadRegL, 0x17);
  byte w1 = rfid.PCD_ReadRegister(MFRC522::TReloadRegL);
  rfid.PCD_WriteRegister(MFRC522::TReloadRegL, 0xE8);
  byte w2 = rfid.PCD_ReadRegister(MFRC522::TReloadRegL);
  bool ok = v != 0x00 && v != 0xFF && p == 0xA9 && r == 0xE8 && (tx & 0x03) == 0x03 &&
            w1 == 0x17 && w2 == 0xE8;
  snprintf(out, n, "v=%02X p=%02X r=%02X tx=%02X w=%02X/%02X %s", v, p, r, tx, w1, w2, ok ? "OK" : "FAIL");
  return ok;
}

static void report(bool force) {
  char line[80];
  bool ok = health(line, sizeof(line));
  if (!ok) everFailed = true;
  if (force || strcmp(line, lastLine) != 0 || millis() - lastPrint >= 5000) {
    Serial.printf("[%c %3lus wifi=%d] %s\n", phase, (millis() - phaseStart) / 1000UL,
                  (int)WiFi.status(), line);
    strncpy(lastLine, line, sizeof(lastLine) - 1);
    lastPrint = millis();
  }
}

void setup() {
  // ปิด WiFi ให้เร็วที่สุด ก่อนแตะ RC522
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("=== RC522_WiFiStress ==="));
  SPI.begin();
  rfid.PCD_Init();
  delay(50);
  phaseStart = millis();
  Serial.println(F("phase A: WiFi OFF. send 'g' to start the 40 s count"));
  report(true);
}

static bool started = false;

void loop() {
  // วนถามหาบัตรเหมือนเครื่องจริง ให้สายอากาศทำงานตามปกติ
  rfid.PICC_IsNewCardPresent();
  delay(40);

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 1000) {
    lastCheck = millis();
    report(false);
  }

  if (phase == 'A') {
    if (!started && (Serial.read() == 'g' || millis() - phaseStart >= 120000UL)) {
      started = true;
      phaseStart = millis();
      Serial.println(F("phase A: counting 40 s with WiFi OFF"));
    }
    if (started && millis() - phaseStart >= 40000UL) {
      phase = 'B';
      phaseStart = millis();
      Serial.printf("phase A done, rc522 failed during A: %s\n", everFailed ? "YES" : "no");
      Serial.println(F("phase B: WiFi ON, connecting"));
      everFailed = false;
      WiFi.forceSleepWake();
      delay(1);
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  } else if (phase == 'B') {
    static int lastStatus = -1;
    if ((int)WiFi.status() != lastStatus) {
      lastStatus = WiFi.status();
      Serial.printf("[B %3lus] wifi status -> %d\n", (millis() - phaseStart) / 1000UL, lastStatus);
    }
    if (millis() - phaseStart >= 60000UL) {
      Serial.printf("phase B done, rc522 failed during B: %s\n", everFailed ? "YES" : "no");
      phase = 'C';
      phaseStart = millis();
      Serial.println(F("phase C: PCD_Init to see if a soft reset recovers it"));
      rfid.PCD_Init();
      delay(50);
      report(true);
      Serial.println(F("=== done ==="));
    }
  }
}
