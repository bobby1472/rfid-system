-- =====================================================================
-- 04  เพิ่มคอลัมน์สุขภาพของเครื่องอ่านให้ตาราง readers ที่มีอยู่แล้ว
-- =====================================================================
--
-- ใช้เฉพาะกับฐานข้อมูลที่สร้างไว้ก่อนมีหน้าจอสถานะ RC522 / จำนวนครั้งที่กู้ RC522
-- ฐานข้อมูลใหม่ที่สร้างจาก 02_schema.sql มีคอลัมน์ครบอยู่แล้ว ไฟล์นี้จะไม่ทำอะไรเลย
--
-- รันซ้ำได้ปลอดภัย ทำงานแบบเดียวกับ backend/app/migrate.py ที่แอปรันเองตอนบูต
-- จึงไม่จำเป็นต้องรันไฟล์นี้ถ้าแอปสตาร์ทผ่าน pm2 อยู่แล้ว
--
-- ค่าเหล่านี้บอร์ดส่งมากับการประกาศตัวทุก 5 นาที ตั้งแต่เฟิร์มแวร์ 1.3.0
-- เครื่องที่ยังเป็นเฟิร์มแวร์รุ่นเก่าจะมีค่าว่างไว้ หน้าเว็บจะแสดงเป็น "—"

SET client_encoding = 'UTF8';

BEGIN;

ALTER TABLE readers ADD COLUMN IF NOT EXISTS firmware               VARCHAR(32);
ALTER TABLE readers ADD COLUMN IF NOT EXISTS rc522_ok               BOOLEAN;
ALTER TABLE readers ADD COLUMN IF NOT EXISTS rc522_recoveries       INTEGER;
-- NOT NULL ใส่ได้ทันทีแม้มีแถวอยู่แล้ว เพราะ DEFAULT 0 จะเติมให้แถวเดิมทุกแถว
ALTER TABLE readers ADD COLUMN IF NOT EXISTS rc522_recoveries_total INTEGER NOT NULL DEFAULT 0;
ALTER TABLE readers ADD COLUMN IF NOT EXISTS uptime_s               INTEGER;

COMMENT ON COLUMN readers.firmware               IS 'Firmware version reported by the board';
COMMENT ON COLUMN readers.rc522_ok               IS 'RC522 health at the last report; NULL = firmware too old to report';
COMMENT ON COLUMN readers.rc522_recoveries       IS 'RC522 self-recoveries since the board last restarted';
COMMENT ON COLUMN readers.rc522_recoveries_total IS 'RC522 self-recoveries accumulated across restarts';
COMMENT ON COLUMN readers.uptime_s               IS 'Seconds since the board restarted, at the last report';

COMMIT;
