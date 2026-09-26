#pragma once
/*
 * แม่แบบของ secrets.h - ไฟล์นี้ commit ได้ ไม่มีค่าจริง
 *
 * วิธีใช้: ก๊อปไฟล์นี้เป็น secrets.h ในโฟลเดอร์เดียวกัน แล้วแก้ค่าให้ถูก
 *   copy secrets.example.h secrets.h
 *
 * secrets.h อยู่ใน .gitignore จึงไม่หลุดขึ้น git
 * แยกออกมาจาก config.h เพราะ config.h ต้อง commit ได้ (มีค่าขา เวลา ฯลฯ)
 * แต่รหัสผ่าน WiFi ห้ามอยู่ใน git เด็ดขาด
 */

#define WIFI_SSID      "ชื่อ WiFi"
#define WIFI_PASSWORD  "รหัสผ่าน WiFi"
