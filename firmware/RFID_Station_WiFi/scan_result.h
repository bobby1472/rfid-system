#pragma once
#include <Arduino.h>

/*
 * ต้องแยกมาไว้ในไฟล์ header เพราะ Arduino IDE จะสร้าง function prototype
 * อัตโนมัติแล้ววางไว้บนสุดของไฟล์ .ino ซึ่งอยู่ก่อนบรรทัดที่ประกาศ struct
 * ทำให้คอมไพเลอร์ไม่รู้จักชนิดข้อมูลนี้ตอนอ่าน prototype
 */
struct ScanResult {
  bool wifiUp;          // WiFi เชื่อมต่ออยู่หรือไม่ (แยกจากปัญหาเซิร์ฟเวอร์)
  bool reachedServer;   // ติดต่อเซิร์ฟเวอร์ได้และได้คำตอบกลับมา
  bool allowed;         // เซิร์ฟเวอร์อนุญาตหรือไม่
  String name;          // ชื่อผู้ถือบัตร (เซิร์ฟเวอร์ตัดมาไม่เกิน 16 ตัวอักษรแล้ว)
  String barcode;       // บาร์โค้ดของบัตร ว่างได้ถ้ายังไม่ได้ผูกไว้
  String message;       // ACCESS OK หรือ DENIED
};
