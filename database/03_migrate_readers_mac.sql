-- =====================================================================
-- 03  เพิ่มคอลัมน์ mac และ registered ให้ตาราง readers ที่มีอยู่แล้ว
-- =====================================================================
--
-- ใช้เฉพาะกับฐานข้อมูลที่สร้างไว้ก่อนมีฟีเจอร์ MAC / "Devices found on the network"
-- ฐานข้อมูลใหม่ที่สร้างจาก 02_schema.sql มีคอลัมน์ครบอยู่แล้ว ไฟล์นี้จะไม่ทำอะไรเลย
--
-- รันซ้ำได้ปลอดภัย ทำงานแบบเดียวกับ backend/app/migrate.py ที่แอปรันเองตอนบูต
-- จึงไม่จำเป็นต้องรันไฟล์นี้ถ้าแอปสตาร์ทผ่าน pm2 อยู่แล้ว มีไว้สำหรับกรณีที่
-- อยากเตรียมฐานข้อมูลเองก่อนเปิดแอป หรือให้ DBA ตรวจก่อนนำไปใช้

SET client_encoding = 'UTF8';

BEGIN;

ALTER TABLE readers ADD COLUMN IF NOT EXISTS mac VARCHAR(17);

-- ต้องเช็คเองว่าคอลัมน์มีอยู่แล้วหรือยัง ไม่ใช้ ADD COLUMN IF NOT EXISTS ตรง ๆ
-- เพราะต้องตั้งแถวเดิมทั้งหมดเป็น registered = true เฉพาะครั้งแรกที่เพิ่มคอลัมน์
-- เครื่องที่มีอยู่ก่อนหน้านี้ถูกตั้งชื่อและใช้งานจริงมาแล้ว ไม่ควรไปโผล่ในกล่อง
-- "พบอุปกรณ์ใหม่" ถ้ารันซ้ำแล้วตั้ง true ทุกครั้ง เครื่องที่เพิ่งถูกค้นพบจะหลุดจากกล่องนั้นไปด้วย
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = current_schema()
          AND table_name   = 'readers'
          AND column_name  = 'registered'
    ) THEN
        ALTER TABLE readers ADD COLUMN registered BOOLEAN NOT NULL DEFAULT false;
        UPDATE readers SET registered = true;
    END IF;
END
$$;

CREATE UNIQUE INDEX IF NOT EXISTS ix_readers_mac ON readers (mac);

COMMENT ON COLUMN readers.mac        IS 'Board MAC address, AA:BB:CC:DD:EE:FF';
COMMENT ON COLUMN readers.registered IS 'false = discovered on the network but not yet confirmed in the web app';

COMMIT;
