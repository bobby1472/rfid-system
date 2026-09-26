/*
 * RC522 Check - ตรวจว่าโมดูล RC522 ตัวที่เสียบอยู่ใช้งานได้หรือไม่ และเสียตรงไหน
 *
 * ใช้การต่อสายชุดเดียวกับ RFID_Station_WiFi ทุกประการ จึงสลับอัปโหลดไปมาได้
 * โดยไม่ต้องขยับสายเลย
 *   SDA/SS -> D8      SCK -> D5      MOSI -> D7      MISO -> D6
 *   VCC -> 3V3        GND -> GND     RST -> ไม่ต่อ    IRQ -> ไม่ต่อ
 *
 * ไม่แตะขา D1/D2 จึงไม่ชนกับจอ LCD ที่ต่อ I2C ไว้ และไม่แตะ D0 จึงไม่ปลุกบัซเซอร์
 *
 * วิธีใช้: อัปโหลด -> เปิด Serial Monitor ที่ 115200 -> กด RST -> อ่านผล
 * มันจะวนตรวจซ้ำทุก 5 วินาที สลับโมดูลขณะเสียบไฟอยู่ก็ได้
 */
#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN D8

MFRC522 rfid(SS_PIN, MFRC522::UNUSED_PIN);   // ไม่ใช้ขา RST เหมือนตัวจริง

static byte readVersion() {
  return rfid.PCD_ReadRegister(MFRC522::VersionReg);
}

static const char *versionName(byte v) {
  switch (v) {
    case 0x88: return "clone FM17522";
    case 0x90: return "NXP v0.0";
    case 0x91: return "NXP v1.0";
    case 0x92: return "NXP v2.0";
    case 0x12: return "counterfeit";
    default:   return "clone (ไม่อยู่ในตารางมาตรฐาน)";
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println(F("===== RC522 CHECK ====="));
  SPI.begin();
}

void loop() {
  rfid.PCD_Init();
  delay(60);

  byte v = readVersion();
  Serial.println();
  Serial.print(F("[1] VersionReg = 0x"));
  if (v < 0x10) Serial.print('0');
  Serial.println(v, HEX);

  if (v == 0x00 || v == 0xFF) {
    Serial.println(F("    >> ชิปไม่ตอบสนอง"));
    if (v == 0x00) {
      Serial.println(F("    อ่านได้ศูนย์ทุกบิต - ไล่ตามลำดับนี้:"));
      Serial.println(F("      1. ไฟเลี้ยง 3.3V กับ GND ถึงโมดูลจริงหรือไม่"));
      Serial.println(F("      2. SCK (D5) หลุด - ไม่มีสัญญาณนาฬิกา ชิปเลยไม่ตอบ"));
      Serial.println(F("      3. MISO (D6) หลุด"));
      Serial.println(F("      4. SDA/SS (D8) หลุด - ชิปไม่เคยถูกเลือก"));
    } else {
      Serial.println(F("    อ่านได้หนึ่งทุกบิต - สาย MISO (D6) ลอย ไม่ได้ต่อถึงโมดูล"));
    }
    Serial.println(F("    สาเหตุที่พบบ่อยที่สุด: pin header ยังไม่ได้บัดกรีเข้ากับบอร์ด"));
    Serial.println(F("======================="));
    delay(5000);
    return;
  }

  Serial.print(F("    >> ชิปตอบสนอง: "));
  Serial.println(versionName(v));

  // เขียนแล้วอ่านกลับ พิสูจน์ว่า MOSI + MISO + SCK + SS ครบวงจรจริง
  byte orig = rfid.PCD_ReadRegister(MFRC522::ModWidthReg);
  byte pat[] = {0x55, 0xAA, 0x0F, 0xF0};
  bool wrOk = true;
  for (byte i = 0; i < 4; i++) {
    rfid.PCD_WriteRegister(MFRC522::ModWidthReg, pat[i]);
    if (rfid.PCD_ReadRegister(MFRC522::ModWidthReg) != pat[i]) wrOk = false;
  }
  rfid.PCD_WriteRegister(MFRC522::ModWidthReg, orig);
  Serial.print(F("[2] เขียน/อ่านกลับ 4 แพทเทิร์น = "));
  Serial.println(wrOk ? F("ผ่าน (SPI ครบวงจร)") : F("ไม่ผ่าน - MOSI(D7) หรือ SCK(D5) ติด ๆ หลุด ๆ"));

  // ภาคสายอากาศ ถ้าไม่ติดจะอ่านบัตรไม่ได้แม้ SPI จะดี
  rfid.PCD_AntennaOn();
  delay(20);
  byte tx = rfid.PCD_ReadRegister(MFRC522::TxControlReg);
  Serial.print(F("[3] TxControlReg = 0x"));
  Serial.print(tx, HEX);
  Serial.println((tx & 0x03) == 0x03 ? F("  สายอากาศเปิด") : F("  สายอากาศไม่ติด - ภาคส่งน่าจะเสีย"));

  // ความแรงของภาครับ ค่าโรงงานคือ 0x48 ถ้าเพี้ยนไปมากแปลว่าชิปมีปัญหา
  byte gain = rfid.PCD_GetAntennaGain();
  Serial.print(F("[4] AntennaGain = 0x"));
  Serial.println(gain, HEX);

  Serial.println(F("[5] ทาบบัตรตอนนี้เพื่อทดสอบการอ่านจริง..."));
  unsigned long until = millis() + 4000;
  bool found = false;
  while (millis() < until) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      Serial.print(F("    อ่านบัตรได้: "));
      for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] < 0x10) Serial.print('0');
        Serial.print(rfid.uid.uidByte[i], HEX);
      }
      Serial.print(F("  ชนิด: "));
      Serial.println(rfid.PICC_GetTypeName(rfid.PICC_GetType(rfid.uid.sak)));
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      found = true;
      break;
    }
    delay(20);
  }
  if (!found) Serial.println(F("    ไม่เจอบัตรในช่วง 4 วินาที (ถ้าไม่ได้ทาบก็ปกติ)"));

  Serial.println(F("======================="));
  delay(2000);
}
